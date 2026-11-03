#include <Arduino.h>
#include "ui.h"
#include "../gsheets.h"
#include "../net.h"

// ---------------------------------------------------------------------------
// Pending Uploads: results that were SAVED on the panel but are not in the
// Google Sheet yet, newest first. Read-only. A row disappears as soon as it
// has been uploaded, so an empty list means the Sheet has everything.
// ---------------------------------------------------------------------------

static const int MAX_SHOWN = 60;
static const int COLS = 6;

static lv_obj_t *s_tbl, *s_summary, *s_empty, *s_status;
static lv_timer_t *s_timer = nullptr;
static PendingRow s_rows[MAX_SHOWN];  // newest first
static int s_shown = 0;
static int s_lastCount = -1;

// Result column: green PASS, red FAIL / ABORTED
static void tbl_draw_cb(lv_event_t *e) {
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part != LV_PART_ITEMS) return;
  uint32_t row = dsc->id / COLS, col = dsc->id % COLS;
  if (row == 0) {
    dsc->rect_dsc->bg_color = COL_HEADER;
    dsc->label_dsc->color = COL_ACCENT;
  } else if (col == 4 && (int)row <= s_shown) {
    bool pass = strcmp(s_rows[row - 1].overall, "PASS") == 0;
    dsc->label_dsc->color = pass ? COL_GREEN : COL_RED;
  }
}

static void reload() {
  int count = gsheets_pending_count();
  s_lastCount = count;
  int skip = count > MAX_SHOWN ? count - MAX_SHOWN : 0;
  int n = gsheets_pending_rows(s_rows, MAX_SHOWN, skip);
  for (int i = 0; i < n / 2; i++) {  // newest first
    PendingRow t = s_rows[i];
    s_rows[i] = s_rows[n - 1 - i];
    s_rows[n - 1 - i] = t;
  }
  s_shown = n;

  if (count == 0) {
    lv_label_set_text(s_summary, "");
  } else if (count > n) {
    lv_label_set_text_fmt(s_summary, "%d results not in the Google Sheet yet (newest %d shown)", count, n);
  } else {
    lv_label_set_text_fmt(s_summary, "%d result%s saved on the panel, not in the Google Sheet yet", count,
                          count == 1 ? "" : "s");
  }
  if (n == 0) {
    lv_obj_add_flag(s_tbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(s_tbl, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);

  lv_table_set_row_cnt(s_tbl, n + 1);
  for (int i = 0; i < n; i++) {
    const PendingRow &p = s_rows[i];
    // "YYYY-MM-DD HH:MM:SS" -> "MM-DD HH:MM"
    char when[16];
    if (strlen(p.ts) >= 16 && p.ts[4] == '-') snprintf(when, sizeof(when), "%.5s %.5s", p.ts + 5, p.ts + 11);
    else strlcpy(when, p.ts, sizeof(when));
    char result[40];
    if (strcmp(p.overall, "FAIL") == 0 && p.failStep[0]) snprintf(result, sizeof(result), "FAIL - %s", p.failStep);
    else strlcpy(result, p.overall, sizeof(result));
    lv_table_set_cell_value(s_tbl, i + 1, 0, when);
    lv_table_set_cell_value(s_tbl, i + 1, 1, p.serial);
    lv_table_set_cell_value(s_tbl, i + 1, 2, p.model);
    lv_table_set_cell_value_fmt(s_tbl, i + 1, 3, "%u", p.ch);
    lv_table_set_cell_value(s_tbl, i + 1, 4, result);
    lv_table_set_cell_value(s_tbl, i + 1, 5, p.op);
  }
}

static void timer_cb(lv_timer_t *) {
  if (gsheets_pending_count() != s_lastCount) reload();  // uploaded rows drop out
  char st[64], buf[120];
  gsheets_status(st, sizeof(st));
  if (s_lastCount == 0) snprintf(buf, sizeof(buf), "Google Sheet: %s", st);
  else snprintf(buf, sizeof(buf), "Uploads automatically when possible.  Google Sheet: %s", st);
  if (strcmp(lv_label_get_text(s_status), buf) != 0) lv_label_set_text(s_status, buf);
}

static void screen_deleted(lv_event_t *e) {
  lv_timer_t *t = (lv_timer_t *)lv_event_get_user_data(e);
  lv_timer_del(t);
  if (s_timer == t) s_timer = nullptr;
}

static void upload_cb(lv_event_t *) {
  if (!gsheets_configured()) {
    ui_toast("Upload", "Google Sheet is not configured on this panel.");
    return;
  }
  if (!net_connected()) {
    ui_toast("Upload", "No WiFi. Connect on the WIFI page;\nthe results upload as soon as it is back.");
    return;
  }
  gsheets_flush_now();
  ui_toast("Upload", "Uploading now...");
}

static void back_cb(lv_event_t *) { ui_show_login(); }

void ui_show_pending() {
  lv_obj_t *scr = ui_screen_create();
  ui_header(scr, "PENDING UPLOADS");
  ui_set_current(UI_PENDING);

  s_summary = lv_label_create(scr);
  lv_obj_set_pos(s_summary, 14, HEADER_H + 8);

  lv_obj_t *box = lv_obj_create(scr);  // scrolls when the list is long
  lv_obj_set_size(box, SCR_W - 24, 270);
  lv_obj_set_pos(box, 12, HEADER_H + 34);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_scroll_dir(box, LV_DIR_VER);

  s_tbl = lv_table_create(box);
  lv_table_set_col_cnt(s_tbl, COLS);
  const char *head[COLS] = {"Finished", "Serial", "Model", "CH", "Result", "Operator"};
  const lv_coord_t w[COLS] = {115, 128, 220, 42, 160, 100};
  for (int c = 0; c < COLS; c++) {
    lv_table_set_col_width(s_tbl, c, w[c]);
    lv_table_set_cell_value(s_tbl, 0, c, head[c]);
  }
  lv_obj_set_style_text_font(s_tbl, FONT_S, LV_PART_ITEMS);
  lv_obj_set_style_pad_top(s_tbl, 7, LV_PART_ITEMS);
  lv_obj_set_style_pad_bottom(s_tbl, 7, LV_PART_ITEMS);
  lv_obj_set_style_pad_left(s_tbl, 8, LV_PART_ITEMS);
  lv_obj_set_style_pad_right(s_tbl, 4, LV_PART_ITEMS);
  lv_obj_clear_flag(s_tbl, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_tbl, tbl_draw_cb, LV_EVENT_DRAW_PART_BEGIN, nullptr);

  s_empty = lv_label_create(scr);
  lv_label_set_text(s_empty, LV_SYMBOL_OK "  Everything is in the Google Sheet");
  lv_obj_set_style_text_font(s_empty, FONT_L, 0);
  lv_obj_set_style_text_color(s_empty, COL_GREEN, 0);
  lv_obj_align(s_empty, LV_ALIGN_TOP_MID, 0, HEADER_H + 130);

  s_status = lv_label_create(scr);
  lv_label_set_text(s_status, "");
  lv_obj_set_style_text_color(s_status, COL_MUTED, 0);
  lv_obj_set_pos(s_status, 14, HEADER_H + 312);

  lv_obj_t *f = ui_footer(scr);
  ui_btn(f, LV_SYMBOL_LEFT " BACK", COL_NEUTRAL, 200, BTN_H, back_cb, nullptr);
  ui_btn(f, LV_SYMBOL_UPLOAD " UPLOAD NOW", COL_PRIMARY, 240, BTN_H, upload_cb, nullptr);

  reload();
  timer_cb(nullptr);
  s_timer = lv_timer_create(timer_cb, 1000, nullptr);
  lv_obj_add_event_cb(scr, screen_deleted, LV_EVENT_DELETE, s_timer);

  ui_load(scr);
}
