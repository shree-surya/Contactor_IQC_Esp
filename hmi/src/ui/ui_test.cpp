#include <Arduino.h>
#include "ui.h"
#include "../app_data.h"
#include "../rig.h"

// Table layout: rows = parameters, columns = channels
enum Row { R_HEAD, R_SERIAL, R_CYCLE, R_OPEN, R_INRUSH, R_CONT, R_CONTIN, R_RELEASE, R_STATUS, ROWS };
static const int COLS = NUM_CH + 1;
static const char *ROW_NAMES[ROWS] = {"Parameter", "Serial", "Cycle", "Open", "In-rush (A)",
                                      "Cont (A)", "Continuity", "Release", "STATUS"};

enum CellStyle : uint8_t {
  CS_PLAIN, CS_HEAD, CS_LABEL, CS_MUTED, CS_PASS, CS_FAIL, CS_RUN,
  CS_ST_IDLE, CS_ST_WAIT, CS_ST_RUN, CS_ST_PASS, CS_ST_FAIL, CS_ST_ABORT
};

static lv_obj_t *s_table, *s_round_lbl, *s_state_lbl, *s_live_lbl, *s_event_lbl;
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
    case CS_HEAD: bg = COL_HEADER; fg = lv_color_white(); break;
    case CS_LABEL: bg = COL_LABEL_BG; break;
    case CS_MUTED: fg = COL_MUTED; break;
    case CS_PASS: bg = COL_PASS_BG; fg = COL_OK; break;
    case CS_FAIL: bg = COL_FAIL_BG; fg = COL_FAIL; break;
    case CS_RUN: bg = COL_RUN_BG; break;
    case CS_ST_IDLE: bg = lv_color_hex(0x9E9E9E); fg = lv_color_white(); break;
    case CS_ST_WAIT: bg = COL_PRIMARY; fg = lv_color_white(); break;
    case CS_ST_RUN: bg = COL_WARN; break;
    case CS_ST_PASS: bg = COL_OK; fg = lv_color_white(); break;
    case CS_ST_FAIL: bg = COL_FAIL; fg = lv_color_white(); break;
    case CS_ST_ABORT: bg = lv_color_hex(0x6D4C41); fg = lv_color_white(); break;
    default: break;
  }
  dsc->rect_dsc->bg_color = bg;
  dsc->rect_dsc->bg_opa = LV_OPA_COVER;
  dsc->label_dsc->color = fg;
  dsc->label_dsc->align = col == 0 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
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
  char live[96] = "";

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
    } else {
      set_cell(R_SERIAL, col, "--", CS_MUTED);
      set_cell(R_CYCLE, col, "--", CS_MUTED);
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

    if (v.liveA > 0.05f) {
      char part[24];
      snprintf(part, sizeof(part), "CH%d %.2f A   ", ch + 1, v.liveA);
      strlcat(live, part, sizeof(live));
    }
  }
  lv_obj_invalidate(s_table);

  lv_label_set_text_fmt(s_round_lbl, "Round %d / %d", rig_round(), m.cycles);
  lv_label_set_text_fmt(s_live_lbl, "Live: %s", live[0] ? live : "-");
  lv_label_set_text(s_event_lbl, rig_last_event());

  if (running) {
    lv_label_set_text(s_state_lbl, "RUNNING");
    lv_obj_set_style_bg_color(s_state_lbl, COL_WARN, 0);
  } else if (s_started && aborted) {
    lv_label_set_text(s_state_lbl, "STOPPED");
    lv_obj_set_style_bg_color(s_state_lbl, COL_FAIL, 0);
  } else if (s_started) {
    lv_label_set_text_fmt(s_state_lbl, "DONE  P:%d F:%d", pass, failed);
    lv_obj_set_style_bg_color(s_state_lbl, failed ? COL_FAIL : COL_OK, 0);
  } else {
    lv_label_set_text(s_state_lbl, "READY");
    lv_obj_set_style_bg_color(s_state_lbl, COL_NEUTRAL, 0);
  }

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

static lv_obj_t *info_label(lv_obj_t *parent, lv_align_t align, lv_coord_t x, lv_coord_t y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, FONT_M, 0);
  lv_obj_align(l, align, x, y);
  return l;
}

void ui_show_test() {
  lv_obj_t *scr = ui_screen_create();
  const ModelSpec &m = g_models[g_app.modelIdx];
  char title[120];
  snprintf(title, sizeof(title), "%s  |  %s", g_app.operatorName, m.name);
  ui_header(scr, title);

  s_round_lbl = info_label(scr, LV_ALIGN_TOP_LEFT, 14, HEADER_H + 8);
  s_live_lbl = info_label(scr, LV_ALIGN_TOP_LEFT, 190, HEADER_H + 10);
  lv_obj_set_style_text_font(s_live_lbl, FONT_S, 0);

  s_state_lbl = lv_label_create(scr);
  lv_obj_set_style_text_font(s_state_lbl, FONT_M, 0);
  lv_obj_set_style_text_color(s_state_lbl, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(s_state_lbl, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s_state_lbl, 6, 0);
  lv_obj_set_style_pad_hor(s_state_lbl, 12, 0);
  lv_obj_set_style_pad_ver(s_state_lbl, 4, 0);
  lv_obj_align(s_state_lbl, LV_ALIGN_TOP_RIGHT, -14, HEADER_H + 6);

  s_table = lv_table_create(scr);
  lv_table_set_col_cnt(s_table, COLS);
  lv_table_set_row_cnt(s_table, ROWS);
  lv_table_set_col_width(s_table, 0, 170);
  for (int c = 1; c < COLS; c++) lv_table_set_col_width(s_table, c, 120);
  lv_obj_set_style_text_font(s_table, FONT_S, LV_PART_ITEMS);
  lv_obj_set_style_pad_top(s_table, 6, LV_PART_ITEMS);
  lv_obj_set_style_pad_bottom(s_table, 6, LV_PART_ITEMS);
  lv_obj_set_style_pad_left(s_table, 6, LV_PART_ITEMS);
  lv_obj_set_style_pad_right(s_table, 4, LV_PART_ITEMS);
  lv_obj_set_style_pad_all(s_table, 0, 0);
  lv_obj_set_style_border_width(s_table, 1, 0);
  lv_obj_clear_flag(s_table, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, HEADER_H + 40);
  lv_obj_add_event_cb(s_table, table_draw_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);

  memset(s_style, CS_PLAIN, sizeof(s_style));
  for (int r = 0; r < ROWS; r++) set_cell(r, 0, ROW_NAMES[r], r == R_HEAD ? CS_HEAD : CS_LABEL);
  for (int c = 1; c < COLS; c++) {
    char name[8];
    snprintf(name, sizeof(name), "CH%d", c);
    set_cell(R_HEAD, c, name, CS_HEAD);
  }

  s_event_lbl = lv_label_create(scr);
  lv_obj_set_width(s_event_lbl, SCR_W - 28);
  lv_label_set_long_mode(s_event_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(s_event_lbl, COL_MUTED, 0);
  lv_obj_align(s_event_lbl, LV_ALIGN_BOTTOM_LEFT, 14, -(FOOTER_H + 2));

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
