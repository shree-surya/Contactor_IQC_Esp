#include <Arduino.h>
#include "ui.h"
#include "../app_data.h"
#include "../rig.h"
#include "../gsheets.h"

// Grid layout: rows = parameters, columns = channels.
// The top-left cell shows the batch state (READY / RUNNING / DONE / STOPPED).
enum Row { R_HEAD, R_LIVE, R_STATUS, R_SERIAL, R_CYCLE, R_OPEN, R_INRUSH, R_CONT, R_CONTIN, R_RELEASE, ROWS };
static const int COLS = NUM_CH + 1;
static const char *ROW_NAMES[ROWS] = {"", "Live (A)", "Status", "Serial", "Cycle", "Open",
                                      "In-rush (A)", "Cont (A)", "Continuity", "Release"};

enum CellStyle : uint8_t {
  CS_PLAIN, CS_HEAD, CS_LABEL, CS_MUTED, CS_LIVE, CS_PASS, CS_FAIL, CS_RUN,
  CS_ST_IDLE, CS_ST_WAIT, CS_ST_RUN, CS_ST_PASS, CS_ST_FAIL, CS_ST_ABORT
};

// The grid is built from one small object per cell (not lv_table): changing a
// value then redraws only that cell. lv_table redraws the whole table on every
// change, which during a test meant ~5 full-screen redraws per second into the
// PSRAM frame buffer and visible flicker on the RGB panel.
static const lv_coord_t COL0_W = 160, COL_W = 124, ROW_H = 34;

static lv_obj_t *s_cell[ROWS][COLS];
static lv_obj_t *s_lbl[ROWS][COLS];
static lv_obj_t *s_btn_back, *s_btn_start, *s_btn_stop, *s_btn_save, *s_btn_home;
static lv_timer_t *s_timer = nullptr;
static uint8_t s_style[ROWS][COLS];
static bool s_started = false;
static bool s_saved = false;  // SAVE pressed for this batch

static void apply_style(int row, int col, CellStyle st) {
  lv_color_t bg = (row % 2) ? COL_CARD : COL_ROW_ALT, fg = COL_TEXT;
  switch (st) {
    case CS_HEAD: bg = COL_HEADER; fg = COL_ACCENT; break;
    case CS_LABEL: bg = COL_LABEL_BG; fg = COL_MUTED; break;
    case CS_MUTED: fg = COL_SOFT; break;
    case CS_LIVE: bg = COL_LIVE_BG; fg = COL_DARK_TEXT; break;
    case CS_PASS: bg = COL_PASS_BG; fg = COL_GREEN; break;
    case CS_FAIL: bg = COL_FAIL_BG; fg = COL_RED; break;
    case CS_RUN: bg = COL_RUN_BG; fg = COL_TEXT; break;
    case CS_ST_IDLE: bg = COL_LABEL_BG; fg = COL_MUTED; break;
    case CS_ST_WAIT: bg = COL_NEUTRAL; fg = COL_ACCENT; break;
    case CS_ST_RUN: bg = COL_ACCENT; fg = COL_DARK_TEXT; break;
    case CS_ST_PASS: bg = COL_GREEN; fg = COL_ON_DARK; break;
    case CS_ST_FAIL: bg = COL_RED; fg = COL_ON_DARK; break;
    case CS_ST_ABORT: bg = COL_ABORT; fg = COL_ON_DARK; break;
    default: break;
  }
  lv_obj_set_style_bg_color(s_cell[row][col], bg, 0);
  lv_obj_set_style_text_color(s_lbl[row][col], fg, 0);
}

// Only touches the cell when its text or colour really changes
static void set_cell(int row, int col, const char *txt, CellStyle st) {
  if (strcmp(lv_label_get_text(s_lbl[row][col]), txt) != 0) lv_label_set_text(s_lbl[row][col], txt);
  if (s_style[row][col] != st) {
    s_style[row][col] = st;
    apply_style(row, col, st);
  }
}

static lv_obj_t *create_grid(lv_obj_t *parent) {
  lv_obj_t *grid = lv_obj_create(parent);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, COL0_W + NUM_CH * COL_W, ROWS * ROW_H);
  lv_obj_set_style_border_width(grid, 1, 0);
  lv_obj_set_style_border_side(grid, (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT), 0);
  lv_obj_set_style_border_color(grid, COL_BORDER, 0);
  lv_obj_clear_flag(grid, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      lv_obj_t *cell = lv_obj_create(grid);
      lv_obj_remove_style_all(cell);
      lv_obj_set_size(cell, c == 0 ? COL0_W : COL_W, ROW_H);
      lv_obj_set_pos(cell, c == 0 ? 0 : COL0_W + (c - 1) * COL_W, r * ROW_H);
      lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(cell, 1, 0);
      lv_obj_set_style_border_side(cell, (lv_border_side_t)(LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM), 0);
      lv_obj_set_style_border_color(cell, COL_BORDER, 0);
      lv_obj_clear_flag(cell, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

      lv_obj_t *lbl = lv_label_create(cell);
      lv_label_set_text(lbl, "");
      lv_obj_set_style_text_font(lbl, FONT_S, 0);
      if (c == 0 && r != R_HEAD) lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
      else lv_obj_center(lbl);

      s_cell[r][c] = cell;
      s_lbl[r][c] = lbl;
      s_style[r][c] = CS_PLAIN;
      apply_style(r, c, CS_PLAIN);
    }
  }
  return grid;
}

static void step_cell(int row, int col, StepState s) {
  switch (s) {
    case ST_RUN: set_cell(row, col, "...", CS_RUN); break;
    case ST_PASS: set_cell(row, col, "PASS", CS_PASS); break;
    case ST_FAIL: set_cell(row, col, "FAIL", CS_FAIL); break;
    case ST_NA: set_cell(row, col, "-NA-", CS_MUTED); break;
    default: set_cell(row, col, "", CS_PLAIN); break;
  }
}

static void value_cell(int row, int col, StepState s, bool measured, float val, int decimals) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.*f", decimals, val);
  switch (s) {
    case ST_RUN: set_cell(row, col, val > 0 ? buf : "...", CS_RUN); break;
    case ST_PASS: set_cell(row, col, buf, CS_PASS); break;
    case ST_FAIL: set_cell(row, col, buf, CS_FAIL); break;
    case ST_NA: set_cell(row, col, measured ? buf : "-NA-", CS_MUTED); break;
    default: set_cell(row, col, "", CS_PLAIN); break;
  }
}

static void refresh() {
  if (g_app.modelIdx < 0) return;  // batch finished, waiting for the upload popup
  const ModelSpec &m = g_models[g_app.modelIdx];
  bool running = rig_running();
  int pass = 0, failed = 0, aborted = 0;

  for (int ch = 0; ch < NUM_CH; ch++) {
    int col = ch + 1;
    const ChannelView &v = rig_channel(ch);
    bool en = g_app.chEnabled[ch];
    char buf[24];

    if (en) {
      const char *s = g_app.serial[ch];
      size_t n = strlen(s);
      if (n > 10) snprintf(buf, sizeof(buf), "..%s", s + n - 8);
      else strlcpy(buf, s, sizeof(buf));
      set_cell(R_SERIAL, col, buf, CS_PLAIN);
      snprintf(buf, sizeof(buf), "%d/%d", v.cycle, m.cycles);
      set_cell(R_CYCLE, col, buf, CS_PLAIN);
      snprintf(buf, sizeof(buf), "%.2f", v.liveA);
      set_cell(R_LIVE, col, buf, v.liveA > 0.05f ? CS_LIVE : CS_MUTED);
    } else {
      set_cell(R_SERIAL, col, "--", CS_MUTED);
      set_cell(R_CYCLE, col, "--", CS_MUTED);
      set_cell(R_LIVE, col, "--", CS_MUTED);
    }

    step_cell(R_OPEN, col, v.open);
    value_cell(R_INRUSH, col, v.inrush, v.inrushMeasured, v.inrushA, 2);
    value_cell(R_CONT, col, v.cont, v.contMeasured, v.contA, 3);
    step_cell(R_CONTIN, col, v.contin);
    step_cell(R_RELEASE, col, v.release);

    switch (v.status) {
      case CH_SKIPPED: set_cell(R_STATUS, col, "SKIP", CS_ST_IDLE); break;
      case CH_IDLE: set_cell(R_STATUS, col, "READY", CS_ST_IDLE); break;
      case CH_WAITING: set_cell(R_STATUS, col, "WAIT", CS_ST_WAIT); break;
      case CH_RUNNING: set_cell(R_STATUS, col, "RUN", CS_ST_RUN); break;
      case CH_PASS: set_cell(R_STATUS, col, "PASS", CS_ST_PASS); pass++; break;
      case CH_FAIL: set_cell(R_STATUS, col, "FAIL", CS_ST_FAIL); failed++; break;
      case CH_ABORTED: set_cell(R_STATUS, col, "ABORT", CS_ST_ABORT); aborted++; break;
    }
  }

  // Batch state in the top-left cell
  if (running) {
    set_cell(R_HEAD, 0, "RUNNING", CS_ST_RUN);
  } else if (s_started && aborted) {
    set_cell(R_HEAD, 0, "STOPPED", CS_ST_FAIL);
  } else if (s_started) {
    char buf[24];
    snprintf(buf, sizeof(buf), "DONE  %d/%d", pass, pass + failed);
    set_cell(R_HEAD, 0, buf, failed ? CS_ST_FAIL : CS_ST_PASS);
  } else {
    set_cell(R_HEAD, 0, "READY", CS_ST_IDLE);
  }

  ui_btn_set_enabled(s_btn_back, !running && !s_started);
  ui_btn_set_enabled(s_btn_start, !running && !s_started);
  ui_btn_set_enabled(s_btn_stop, running);
  ui_btn_set_enabled(s_btn_save, !running && s_started && !s_saved);
  ui_btn_set_enabled(s_btn_home, !running && s_started);
}

static void timer_cb(lv_timer_t *) { refresh(); }

// Deletes the refresh timer that belongs to the screen being deleted
static void screen_deleted(lv_event_t *e) {
  lv_timer_t *t = (lv_timer_t *)lv_event_get_user_data(e);
  lv_timer_del(t);
  if (s_timer == t) s_timer = nullptr;
}

static void back_cb(lv_event_t *) { ui_show_model(); }

static void start_cb(lv_event_t *) {
  rig_start(&g_models[g_app.modelIdx], g_app.chEnabled);
  s_started = true;
  refresh();
}

static void stop_cb(lv_event_t *) {
  rig_stop_all();
  refresh();
}

// ---------------------------------------------------------------------------
// SAVE: the operator decides whether this batch goes to the Google Sheet.
// Finished units wait in RAM during the test (writing flash while the panel
// is drawing makes the screen glitch); SAVE stores them in the upload queue
// and uploads them. Offline, they stay queued and upload automatically later.
// ---------------------------------------------------------------------------

static const uint32_t SYNC_TIMEOUT_MS = 20000;
static lv_obj_t *s_sync_ov = nullptr, *s_sync_lbl = nullptr, *s_sync_spin = nullptr;
static uint32_t s_sync_t0 = 0;
static int s_sync_phase = 0;  // 0 = uploading, 1 = success shown, 2 = offline note shown

static lv_obj_t *overlay() {
  lv_obj_t *ov = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ov, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_50, 0);
  lv_obj_set_style_radius(ov, 0, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  return ov;
}

static lv_obj_t *panel(lv_obj_t *ov, lv_coord_t w, lv_coord_t h) {
  lv_obj_t *p = lv_obj_create(ov);
  lv_obj_set_size(p, w, h);
  lv_obj_center(p);
  lv_obj_set_style_border_side(p, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(p, 4, 0);
  lv_obj_set_style_border_color(p, COL_ACCENT, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

static void sync_msg(const char *txt, lv_color_t color) {
  lv_label_set_text(s_sync_lbl, txt);
  lv_obj_set_style_text_color(s_sync_lbl, color, 0);
  lv_obj_add_flag(s_sync_spin, LV_OBJ_FLAG_HIDDEN);
  lv_obj_center(s_sync_lbl);
  s_sync_t0 = millis();
}

static void sync_timer_cb(lv_timer_t *t) {
  uint32_t el = millis() - s_sync_t0;
  if (s_sync_phase == 0) {
    if (!gsheets_configured()) {
      sync_msg("Results saved on the panel.\nGoogle Sheet is not configured.", COL_MUTED);
      s_sync_phase = 2;
    } else if (gsheets_pending_count() == 0) {
      sync_msg(LV_SYMBOL_OK "  Saved to Google Sheet", COL_GREEN);
      s_sync_phase = 1;
    } else if (el > SYNC_TIMEOUT_MS) {
      sync_msg(LV_SYMBOL_WARNING "  No connection.\nResults are saved on the panel\nand will upload automatically.", COL_WARN_TXT);
      s_sync_phase = 2;
    }
    return;
  }
  if (el < (s_sync_phase == 1 ? 1200u : 3500u)) return;
  lv_timer_del(t);
  lv_obj_del(s_sync_ov);
  s_sync_ov = nullptr;
}

static void save_cb(lv_event_t *) {
  ResultRow row;
  while (rig_pop_result(&row)) gsheets_enqueue(row);
  s_saved = true;
  ui_btn_set_enabled(s_btn_save, false);
  lv_label_set_text(lv_obj_get_child(s_btn_save, 0), LV_SYMBOL_OK " SAVED");

  s_sync_ov = overlay();
  lv_obj_t *p = panel(s_sync_ov, 460, 230);
  s_sync_spin = lv_spinner_create(p, 1000, 60);
  lv_obj_set_size(s_sync_spin, 70, 70);
  lv_obj_align(s_sync_spin, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_arc_color(s_sync_spin, COL_ACCENT, LV_PART_INDICATOR);

  s_sync_lbl = lv_label_create(p);
  lv_label_set_text(s_sync_lbl, "Saving to Google Sheet...");
  lv_obj_set_style_text_font(s_sync_lbl, FONT_M, 0);
  lv_obj_set_style_text_align(s_sync_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_sync_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);

  s_sync_phase = 0;
  s_sync_t0 = millis();
  gsheets_flush_now();
  lv_timer_create(sync_timer_cb, 250, nullptr);
}

// ---------------------------------------------------------------------------
// HOME: back to the model / serial page for the next batch. Unsaved results
// need a confirmation, then they are discarded.
// ---------------------------------------------------------------------------

static lv_obj_t *s_confirm_ov = nullptr;

static void go_home() {
  rig_clear_results();
  // Keep the operator; the model, serials and ticks are chosen again for every batch
  g_app.modelIdx = -1;
  for (int i = 0; i < NUM_CH; i++) {
    g_app.serial[i][0] = '\0';
    g_app.chEnabled[i] = false;
  }
  bool none[NUM_CH] = {false};
  rig_reset(none);
  ui_show_model();
}

static void confirm_close() {
  lv_obj_del(s_confirm_ov);
  s_confirm_ov = nullptr;
}

static void discard_cb(lv_event_t *) {
  confirm_close();
  go_home();
}

static void cancel_cb(lv_event_t *) { confirm_close(); }

static void home_cb(lv_event_t *) {
  if (s_saved || rig_pending_count() == 0) {
    go_home();
    return;
  }
  s_confirm_ov = overlay();
  lv_obj_t *p = panel(s_confirm_ov, 500, 240);
  lv_obj_t *t = lv_label_create(p);
  lv_label_set_text(t, LV_SYMBOL_WARNING "  Results not saved");
  lv_obj_set_style_text_font(t, FONT_L, 0);
  lv_obj_set_style_text_color(t, COL_WARN_TXT, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_t *m = lv_label_create(p);
  lv_label_set_text(m, "Go home without sending this batch\nto the Google Sheet?");
  lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(m, LV_ALIGN_TOP_MID, 0, 52);
  lv_obj_t *c = ui_btn(p, "CANCEL", COL_NEUTRAL, 200, BTN_H, cancel_cb, nullptr);
  lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *d = ui_btn(p, "DON'T SAVE", COL_FAIL, 200, BTN_H, discard_cb, nullptr);
  lv_obj_align(d, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void ui_show_test() {
  lv_obj_t *scr = ui_screen_create();
  const ModelSpec &m = g_models[g_app.modelIdx];
  char title[120];
  snprintf(title, sizeof(title), "Login by: %s  |  %s", g_app.operatorName, m.name);
  ui_header(scr, title);
  ui_set_current(UI_TEST);

  lv_obj_t *grid = create_grid(scr);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, HEADER_H + 8);

  for (int r = 1; r < ROWS; r++) set_cell(r, 0, ROW_NAMES[r], CS_LABEL);
  for (int c = 1; c < COLS; c++) {
    char name[8];
    snprintf(name, sizeof(name), "CH%d", c);
    set_cell(R_HEAD, c, name, CS_HEAD);
  }

  lv_obj_t *f = ui_footer(scr);
  s_btn_back = ui_btn(f, LV_SYMBOL_LEFT " BACK", COL_NEUTRAL, 136, BTN_H, back_cb, nullptr);
  s_btn_start = ui_btn(f, LV_SYMBOL_PLAY " START", COL_OK, 150, BTN_H, start_cb, nullptr);
  s_btn_stop = ui_btn(f, LV_SYMBOL_STOP " STOP ALL", COL_FAIL, 172, BTN_H, stop_cb, nullptr);
  s_btn_save = ui_btn(f, LV_SYMBOL_UPLOAD " SAVE", COL_HEADER, 142, BTN_H, save_cb, nullptr);
  s_btn_home = ui_btn(f, LV_SYMBOL_HOME " HOME", COL_PRIMARY, 142, BTN_H, home_cb, nullptr);

  s_started = false;
  s_saved = false;
  rig_reset(g_app.chEnabled);
  refresh();
  s_timer = lv_timer_create(timer_cb, 150, nullptr);
  lv_obj_add_event_cb(scr, screen_deleted, LV_EVENT_DELETE, s_timer);

  ui_load(scr);
}
