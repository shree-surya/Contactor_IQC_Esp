#include <Arduino.h>
#include "ui.h"
#include "../app_data.h"
#include "../net.h"
#include "../rig.h"
#include "../gsheets.h"

static void (*s_back)() = nullptr;
static lv_obj_t *s_status_lbl = nullptr;
static lv_timer_t *s_timer = nullptr;

static void back_cb(lv_event_t *) {
  if (s_back) s_back();
}

static void sync_cb(lv_event_t *) {
  if (!gsheets_configured()) {
    ui_toast("Sync", "Google Sheets is not configured.\nAdd include/secrets.h and rebuild.");
    return;
  }
  gsheets_request_sync();
  ui_toast("Sync", "Reading Specs and Operators from the Google Sheet.\nThe tables refresh when it is done.");
}

static lv_obj_t *make_table(lv_obj_t *parent, int cols, int rows) {
  lv_obj_t *t = lv_table_create(parent);
  lv_table_set_col_cnt(t, cols);
  lv_table_set_row_cnt(t, rows);
  lv_obj_set_style_text_font(t, FONT_S, LV_PART_ITEMS);
  lv_obj_set_style_pad_top(t, 6, LV_PART_ITEMS);
  lv_obj_set_style_pad_bottom(t, 6, LV_PART_ITEMS);
  lv_obj_set_style_pad_left(t, 8, LV_PART_ITEMS);
  lv_obj_set_style_pad_right(t, 4, LV_PART_ITEMS);
  lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
  return t;
}

static void fmt_limit(char *buf, size_t n, float v) {
  if (isnan(v)) strlcpy(buf, "NA", n);
  else snprintf(buf, n, "%.2f", v);
}

static void build_specs_tab(lv_obj_t *tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab, 10, 0);

  lv_obj_t *t = make_table(tab, 6, g_modelCount + 1);
  const char *head[] = {"Model", "IR LSL", "IR USL", "Cont LSL", "Cont USL", "Cycles"};
  const int widths[] = {270, 85, 85, 100, 100, 85};
  for (int c = 0; c < 6; c++) {
    lv_table_set_col_width(t, c, widths[c]);
    lv_table_set_cell_value(t, 0, c, head[c]);
  }
  for (int i = 0; i < g_modelCount; i++) {
    const ModelSpec &m = g_models[i];
    char b[16];
    lv_table_set_cell_value(t, i + 1, 0, m.name);
    fmt_limit(b, sizeof(b), m.inrushLsl);
    lv_table_set_cell_value(t, i + 1, 1, b);
    fmt_limit(b, sizeof(b), m.inrushUsl);
    lv_table_set_cell_value(t, i + 1, 2, b);
    fmt_limit(b, sizeof(b), m.contLsl);
    lv_table_set_cell_value(t, i + 1, 3, b);
    fmt_limit(b, sizeof(b), m.contUsl);
    lv_table_set_cell_value(t, i + 1, 4, b);
    lv_table_set_cell_value_fmt(t, i + 1, 5, "%d", m.cycles);
  }

  lv_obj_t *l = lv_label_create(tab);
  lv_label_set_text(l, "Timings (ms)");
  lv_obj_set_style_text_font(l, FONT_M, 0);

  lv_obj_t *t2 = make_table(tab, 7, g_modelCount + 1);
  const char *head2[] = {"Model", "In-rush", "Settle", "Avg", "Release", "Gap", "Stagger"};
  const int widths2[] = {270, 75, 70, 65, 80, 65, 80};
  for (int c = 0; c < 7; c++) {
    lv_table_set_col_width(t2, c, widths2[c]);
    lv_table_set_cell_value(t2, 0, c, head2[c]);
  }
  for (int i = 0; i < g_modelCount; i++) {
    const ModelSpec &m = g_models[i];
    lv_table_set_cell_value(t2, i + 1, 0, m.name);
    lv_table_set_cell_value_fmt(t2, i + 1, 1, "%u", m.inrushWinMs);
    lv_table_set_cell_value_fmt(t2, i + 1, 2, "%u", m.settleMs);
    lv_table_set_cell_value_fmt(t2, i + 1, 3, "%u", m.avgMs);
    lv_table_set_cell_value_fmt(t2, i + 1, 4, "%u", m.releaseMs);
    lv_table_set_cell_value_fmt(t2, i + 1, 5, "%u", m.cycleGapMs);
    lv_table_set_cell_value_fmt(t2, i + 1, 6, "%u", m.staggerMs);
  }
}

static void build_operators_tab(lv_obj_t *tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab, 8, 0);
  for (int i = 0; i < g_operatorCount; i++) {
    lv_obj_t *l = lv_label_create(tab);
    lv_label_set_text_fmt(l, "%d.  %s", i + 1, g_operators[i]);
    lv_obj_set_style_text_font(l, FONT_M, 0);
  }
  lv_obj_t *n = lv_label_create(tab);
  lv_label_set_text(n, "Names come from the \"Operators\" tab of the Google Sheet (max 10).");
  lv_obj_set_style_text_color(n, COL_MUTED, 0);
}

static void refresh_status() {
  if (!s_status_lbl) return;
  char ssid[40], ip[20], gs[64], last[24], now[24];
  net_saved_ssid(ssid, sizeof(ssid));
  net_ip(ip, sizeof(ip));
  gsheets_status(gs, sizeof(gs));
  gsheets_last_sync(last, sizeof(last));
  gsheets_now_str(now, sizeof(now));
  lv_label_set_text_fmt(s_status_lbl,
                        "WiFi network:   %s\n"
                        "WiFi status:    %s (RSSI %d dBm)   IP %s\n"
                        "Google Sheet:   %s\n"
                        "Last sync:      %s\n"
                        "Rows to upload: %d\n"
                        "Clock:          %s\n"
                        "Test rig:       %s\n"
                        "Firmware:       " FW_VERSION,
                        ssid[0] ? ssid : "(not set - see Admin)", net_connected() ? "connected" : "not connected",
                        net_rssi(), ip, gs, last, gsheets_pending_count(), now,
                        rig_is_simulated() ? "SIMULATION (sub-board not connected)" : "connected");
}

static void timer_cb(lv_timer_t *) { refresh_status(); }

// Deletes the refresh timer that belongs to the screen being deleted
static void screen_deleted(lv_event_t *e) {
  lv_timer_t *t = (lv_timer_t *)lv_event_get_user_data(e);
  lv_timer_del(t);
  if (s_timer == t) {
    s_timer = nullptr;
    s_status_lbl = nullptr;
  }
}

void ui_setup_refresh() { ui_show_setup(s_back); }

void ui_show_setup(void (*back)()) {
  s_back = back;
  lv_obj_t *scr = ui_screen_create();
  ui_header(scr, "SETUP");
  ui_set_current(UI_SETUP);

  lv_obj_t *tv = lv_tabview_create(scr, LV_DIR_TOP, 46);
  lv_obj_set_size(tv, SCR_W, SCR_H - HEADER_H - FOOTER_H);
  lv_obj_set_pos(tv, 0, HEADER_H);
  lv_obj_set_style_text_font(lv_tabview_get_tab_btns(tv), FONT_M, 0);

  build_specs_tab(lv_tabview_add_tab(tv, "Specs"));
  build_operators_tab(lv_tabview_add_tab(tv, "Operators"));

  lv_obj_t *st = lv_tabview_add_tab(tv, "Status");
  s_status_lbl = lv_label_create(st);
  lv_obj_set_style_text_font(s_status_lbl, FONT_M, 0);
  lv_obj_set_style_text_line_space(s_status_lbl, 6, 0);
  refresh_status();
  s_timer = lv_timer_create(timer_cb, 1000, nullptr);
  lv_obj_add_event_cb(scr, screen_deleted, LV_EVENT_DELETE, s_timer);

  lv_obj_t *f = ui_footer(scr);
  ui_btn(f, LV_SYMBOL_LEFT " BACK", COL_NEUTRAL, 200, BTN_H, back_cb, nullptr);
  ui_btn(f, LV_SYMBOL_REFRESH " SYNC NOW", COL_PRIMARY, 220, BTN_H, sync_cb, nullptr);

  ui_load(scr);
}
