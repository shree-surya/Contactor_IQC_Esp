#include <Arduino.h>
#include "ui.h"
#include "../app_data.h"
#include "../rig.h"

// Table layout: rows = parameters, columns = channels.
// The top-left cell shows the batch state (READY / RUNNING / DONE / STOPPED).
enum Row { R_HEAD, R_SERIAL, R_CYCLE, R_LIVE, R_OPEN, R_INRUSH, R_CONT, R_CONTIN, R_RELEASE, R_STATUS, ROWS };
static const int COLS = NUM_CH + 1;
static const char *ROW_NAMES[ROWS] = {"", "Serial", "Cycle", "Live (A)", "Open", "In-rush (A)",
                                      "Cont (A)", "Continuity", "Release", "STATUS"};

enum CellStyle : uint8_t {
  CS_PLAIN, CS_HEAD, CS_LABEL, CS_MUTED, CS_LIVE, CS_PASS, CS_FAIL, CS_RUN,
  CS_ST_IDLE, CS_ST_WAIT, CS_ST_RUN, CS_ST_PASS, CS_ST_FAIL, CS_ST_ABORT
};

static lv_obj_t *s_table;
static lv_obj_t *s_btn_back, *s_btn_start, *s_btn_stop, *s_btn_next;
static lv_timer_t *s_timer = nullptr;
static uint8_t s_style[ROWS][COLS];
static bool s_started = false;

static void set_cell(int row, int col, const char *txt, CellStyle st) {
  const char *cur = lv_table_get_cell_value(s_table, row, col);
  if (!cur || strcmp(cur, txt) != 0) lv_table_set_cell_value(s_table, row, col, txt);
  s_style[row][col] = st;
}

static void table_draw_cb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part != LV_PART_ITEMS || !dsc->rect_dsc || !dsc->label_dsc) return;
  uint32_t row = dsc->id / COLS;
  uint32_t col = dsc->id % COLS;
  if (row >= ROWS) return;

  lv_color_t bg = COL_CARD, fg = COL_TEXT;
  switch (s_style[row][col]) {
    case CS_HEAD: bg = COL_HEADER; fg = COL_CYAN; break;
    case CS_LABEL: bg = COL_LABEL_BG; fg = COL_MUTED; break;
    case CS_MUTED: fg = COL_MUTED; break;
    case CS_LIVE: fg = COL_CYAN; break;
    case CS_PASS: bg = COL_PASS_BG; fg = COL_LIME; break;
    case CS_FAIL: bg = COL_FAIL_BG; fg = COL_RED; break;
    case CS_RUN: bg = COL_RUN_BG; fg = COL_ORANGE; break;
    case CS_ST_IDLE: bg = COL_NEUTRAL; fg = COL_MUTED; break;
    case CS_ST_WAIT: bg = COL_CYAN; fg = COL_DARK_TEXT; break;
    case CS_ST_RUN: bg = COL_ORANGE; fg = COL_DARK_TEXT; break;
    case CS_ST_PASS: bg = COL_LIME; fg = COL_DARK_TEXT; break;
    case CS_ST_FAIL: bg = COL_RED; fg = COL_DARK_TEXT; break;
    case CS_ST_ABORT: bg = COL_ABORT; fg = COL_TEXT; break;
    default: break;
  }
  dsc->rect_dsc->bg_color = bg;
  dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  dsc->rect_dsc->border_color = COL_BORDER;
  dsc->label_dsc->color = fg;
  dsc->label_dsc->align = (col == 0 && row != R_HEAD) ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
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
  lv_obj_invalidate(s_table);

  ui_btn_set_enabled(s_btn_back, !running && !s_started);
  ui_btn_set_enabled(s_btn_start, !running && !s_started);
  ui_btn_set_enabled(s_btn_stop, running);
  ui_btn_set_enabled(s_btn_next, !running && s_started);
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

static void next_cb(lv_event_t *) {
  // Keep operator and model; clear serials and ticks for the next batch
  for (int i = 0; i < NUM_CH; i++) {
    g_app.serial[i][0] = '\0';
    g_app.chEnabled[i] = false;
  }
  bool none[NUM_CH] = {false};
  rig_reset(none);
  ui_show_model();
}

void ui_show_test() {
  lv_obj_t *scr = ui_screen_create();
  const ModelSpec &m = g_models[g_app.modelIdx];
  char title[120];
  snprintf(title, sizeof(title), "%s  |  %s", g_app.operatorName, m.name);
  ui_header(scr, title);

  s_table = lv_table_create(scr);
  lv_table_set_col_cnt(s_table, COLS);
  lv_table_set_row_cnt(s_table, ROWS);
  lv_table_set_col_width(s_table, 0, 160);
  for (int c = 1; c < COLS; c++) lv_table_set_col_width(s_table, c, 124);
  lv_obj_set_style_text_font(s_table, FONT_S, LV_PART_ITEMS);
  lv_obj_set_style_pad_top(s_table, 7, LV_PART_ITEMS);
  lv_obj_set_style_pad_bottom(s_table, 7, LV_PART_ITEMS);
  lv_obj_set_style_pad_left(s_table, 8, LV_PART_ITEMS);
  lv_obj_set_style_pad_right(s_table, 4, LV_PART_ITEMS);
  lv_obj_set_style_border_width(s_table, 1, LV_PART_ITEMS);
  lv_obj_set_style_pad_all(s_table, 0, 0);
  lv_obj_set_style_border_width(s_table, 1, 0);
  lv_obj_set_style_border_color(s_table, COL_BORDER, 0);
  lv_obj_set_style_bg_color(s_table, COL_CARD, 0);
  lv_obj_clear_flag(s_table, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, HEADER_H + 8);
  lv_obj_add_event_cb(s_table, table_draw_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);

  memset(s_style, CS_PLAIN, sizeof(s_style));
  for (int r = 1; r < ROWS; r++) set_cell(r, 0, ROW_NAMES[r], CS_LABEL);
  for (int c = 1; c < COLS; c++) {
    char name[8];
    snprintf(name, sizeof(name), "CH%d", c);
    set_cell(R_HEAD, c, name, CS_HEAD);
  }

  lv_obj_t *f = ui_footer(scr);
  s_btn_back = ui_btn(f, LV_SYMBOL_LEFT " BACK", COL_NEUTRAL, 170, BTN_H, back_cb, nullptr);
  s_btn_start = ui_btn(f, LV_SYMBOL_PLAY " START", COL_OK, 190, BTN_H, start_cb, nullptr);
  s_btn_stop = ui_btn(f, LV_SYMBOL_STOP " STOP ALL", COL_FAIL, 200, BTN_H, stop_cb, nullptr);
  s_btn_next = ui_btn(f, "NEXT BATCH " LV_SYMBOL_RIGHT, COL_PRIMARY, 190, BTN_H, next_cb, nullptr);

  s_started = false;
  rig_reset(g_app.chEnabled);
  refresh();
  s_timer = lv_timer_create(timer_cb, 150, nullptr);
  lv_obj_add_event_cb(scr, screen_deleted, LV_EVENT_DELETE, s_timer);

  ui_load(scr);
}
