#include <Arduino.h>
#include "ui.h"
#include "../net.h"

// ---------------------------------------------------------------------------
// WiFi Networks (admin password): nearby networks from a scan plus the saved
// ones, strongest first. Tap one, type the password, CONNECT saves it and
// connects straight away. FORGET deletes a saved network. The panel keeps
// retrying the saved networks on its own (net.cpp).
// ---------------------------------------------------------------------------

static const int MAX_ROWS = NET_MAX_SCAN + NET_MAX_SAVED;
static const lv_coord_t LIST_W = 470, LIST_H = 300, ROW_H = 52;
static const lv_coord_t PANEL_X = 494, PANEL_W = 294;

static lv_obj_t *s_list, *s_panel_name, *s_panel_info, *s_pass_lbl, *s_pass_ta, *s_eye_lbl;
static lv_obj_t *s_btn_connect, *s_btn_forget, *s_btn_scan, *s_status_lbl, *s_kb;
static lv_obj_t *s_forget_ov = nullptr;
static lv_timer_t *s_timer = nullptr;

static char s_rowSsid[MAX_ROWS][33];
static int s_rows = 0;
static char s_sel[33] = "";
static char s_sig[160] = "";  // what the list currently shows, to rebuild only on change
static bool s_wasScanning = false;

static int rssi_bars(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  return 1;
}

static void add_bars(lv_obj_t *parent, int rssi) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, 26, 18);
  lv_obj_clear_flag(box, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  int n = rssi_bars(rssi);
  for (int i = 0; i < 4; i++) {
    lv_obj_t *b = lv_obj_create(box);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 5, 6 + i * 4);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, i * 7, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, i < n ? COL_TEXT : COL_BORDER, 0);
    lv_obj_set_style_radius(b, 1, 0);
  }
}

static void add_pill(lv_obj_t *parent, const char *txt, lv_color_t bg, lv_color_t fg) {
  lv_obj_t *p = lv_label_create(parent);
  lv_label_set_text(p, txt);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(p, bg, 0);
  lv_obj_set_style_text_color(p, fg, 0);
  lv_obj_set_style_radius(p, 10, 0);
  lv_obj_set_style_pad_hor(p, 8, 0);
  lv_obj_set_style_pad_ver(p, 2, 0);
}


static void row_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= s_rows) return;
  strlcpy(s_sel, s_rowSsid[i], sizeof(s_sel));
  lv_textarea_set_text(s_pass_ta, "");
  s_sig[0] = '\0';  // rebuild to move the highlight: on the next timer run, not
  if (s_timer) lv_timer_ready(s_timer);  // here (this row is deleted by the rebuild)
}

// Rows: scanned networks (strongest first), then saved ones not in range
static void build_list() {
  const NetScanItem *scan;
  int n = net_scan_results(&scan);
  char cur[33];
  net_saved_ssid(cur, sizeof(cur));

  char sig[sizeof(s_sig)];
  snprintf(sig, sizeof(sig), "%d|%d|%s|%s|%d|%d", n, net_saved_count(), net_connected() ? cur : "", s_sel,
           n ? scan[0].rssi / 6 : 0, n > 1 ? scan[1].rssi / 6 : 0);
  if (strcmp(sig, s_sig) == 0) return;
  strlcpy(s_sig, sig, sizeof(s_sig));

  lv_obj_clean(s_list);
  s_rows = 0;
  auto add_row = [&](const char *ssid, int rssi, bool inRange) {
    if (s_rows >= MAX_ROWS) return;
    int idx = s_rows++;
    strlcpy(s_rowSsid[idx], ssid, sizeof(s_rowSsid[idx]));
    bool sel = strcmp(ssid, s_sel) == 0;

    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, sel ? COL_RUN_BG : COL_CARD, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, COL_BORDER, 0);
    if (sel) {
      lv_obj_set_style_outline_width(row, 0, 0);
      lv_obj_set_style_shadow_width(row, 0, 0);
    }
    lv_obj_set_style_pad_hor(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, ssid);
    lv_obj_set_style_text_font(name, FONT_M, 0);
    lv_obj_set_flex_grow(name, 1);
    if (!inRange) lv_obj_set_style_text_color(name, COL_MUTED, 0);

    if (net_is_current(ssid)) add_pill(row, "CONNECTED", COL_PASS_BG, COL_GREEN);
    else if (net_is_saved(ssid)) add_pill(row, "SAVED", COL_LABEL_BG, COL_TEXT);
    if (inRange) {
      add_bars(row, rssi);
    } else {
      lv_obj_t *l = lv_label_create(row);
      lv_label_set_text(l, "not in range");
      lv_obj_set_style_text_color(l, COL_MUTED, 0);
    }
  };

  for (int i = 0; i < n; i++) add_row(scan[i].ssid, scan[i].rssi, true);
  for (int i = 0; i < net_saved_count(); i++) {
    const char *s = net_saved_at(i);
    bool seen = false;
    for (int k = 0; k < n; k++) seen |= strcmp(scan[k].ssid, s) == 0;
    if (!seen) add_row(s, net_is_current(s) ? net_rssi() : -100, net_is_current(s));
  }
  if (s_rows == 0) {
    lv_obj_t *l = lv_label_create(s_list);
    lv_label_set_text(l, net_scan_busy() ? "Scanning..." : "No networks found. Press SCAN.");
    lv_obj_set_style_text_color(l, COL_MUTED, 0);
    lv_obj_set_style_pad_all(l, 14, 0);
  }
}

static void update_panel() {
  bool has = s_sel[0] != '\0';
  bool saved = has && net_is_saved(s_sel);
  lv_label_set_text(s_panel_name, has ? s_sel : "Select a network");

  char info[80];
  lv_color_t col = COL_MUTED;
  if (!has) {
    strlcpy(info, "Tap a network on the left.", sizeof(info));
  } else if (net_is_current(s_sel)) {
    char ip[20];
    net_ip(ip, sizeof(ip));
    snprintf(info, sizeof(info), "Connected, %d dBm\nIP %s", net_rssi(), ip);
    col = COL_GREEN;
  } else if (saved) {
    strlcpy(info, "Saved. CONNECT uses the saved password.", sizeof(info));
  } else {
    strlcpy(info, "Not saved. Enter the password.", sizeof(info));
  }
  lv_label_set_text(s_panel_info, info);
  lv_obj_set_style_text_color(s_panel_info, col, 0);

  ui_btn_set_enabled(s_btn_connect, has && !net_is_current(s_sel));
  ui_btn_set_enabled(s_btn_forget, saved);
  ui_btn_set_enabled(s_pass_ta, has);
}

static void timer_cb(lv_timer_t *) {
  bool busy = net_scan_busy();
  if (busy != s_wasScanning) {
    s_wasScanning = busy;
    lv_label_set_text(lv_obj_get_child(s_btn_scan, 0), busy ? "SCANNING..." : LV_SYMBOL_REFRESH " SCAN");
    ui_btn_set_enabled(s_btn_scan, !busy);
  }
  build_list();
  update_panel();
  char st[80];
  net_status_text(st, sizeof(st));
  char buf[120];
  snprintf(buf, sizeof(buf), "%s.  Saved networks retry every %d s.", st, NET_RETRY_MS / 1000);
  if (strcmp(lv_label_get_text(s_status_lbl), buf) != 0) lv_label_set_text(s_status_lbl, buf);
}

static void screen_deleted(lv_event_t *e) {
  lv_timer_t *t = (lv_timer_t *)lv_event_get_user_data(e);
  lv_timer_del(t);
  if (s_timer == t) s_timer = nullptr;
  if (s_forget_ov) {
    lv_obj_del(s_forget_ov);
    s_forget_ov = nullptr;
  }
}

static void hide_kb() {
  lv_keyboard_set_textarea(s_kb, nullptr);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_state(s_pass_ta, LV_STATE_FOCUSED);
}

static void connect_now() {
  hide_kb();
  if (!s_sel[0]) return;
  const char *pass = lv_textarea_get_text(s_pass_ta);
  bool needPass = !net_is_saved(s_sel);
  const NetScanItem *scan;
  int n = net_scan_results(&scan);
  for (int i = 0; i < n; i++) {
    if (strcmp(scan[i].ssid, s_sel) == 0 && !scan[i].secure) needPass = false;  // open network
  }
  if (needPass && !pass[0]) {
    ui_toast("WiFi", "Enter the password for \"%s\".", s_sel);
    return;
  }
  if (!net_connect(s_sel, pass)) {
    ui_toast("WiFi", "%d networks are saved already.\nFORGET one first.", NET_MAX_SAVED);
    return;
  }
  lv_textarea_set_text(s_pass_ta, "");
  s_sig[0] = '\0';
}

static void connect_cb(lv_event_t *) { connect_now(); }

static void pass_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(s_kb, s_pass_ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_READY) {  // keyboard tick = connect
    connect_now();
  } else if (code == LV_EVENT_CANCEL) {
    hide_kb();
  }
}

static void eye_cb(lv_event_t *) {
  bool hidden = !lv_textarea_get_password_mode(s_pass_ta);
  lv_textarea_set_password_mode(s_pass_ta, hidden);
  lv_label_set_text(s_eye_lbl, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void forget_close() {
  lv_obj_del(s_forget_ov);
  s_forget_ov = nullptr;
}

static void forget_cancel_cb(lv_event_t *) { forget_close(); }

static void forget_go_cb(lv_event_t *) {
  forget_close();
  net_forget(s_sel);
  s_sig[0] = '\0';
}

static void forget_cb(lv_event_t *) {
  if (!s_sel[0] || s_forget_ov) return;
  hide_kb();
  s_forget_ov = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_forget_ov, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(s_forget_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_forget_ov, LV_OPA_50, 0);
  lv_obj_set_style_radius(s_forget_ov, 0, 0);
  lv_obj_set_style_border_width(s_forget_ov, 0, 0);
  lv_obj_clear_flag(s_forget_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *p = lv_obj_create(s_forget_ov);
  lv_obj_set_size(p, 460, 210);
  lv_obj_center(p);
  lv_obj_set_style_border_side(p, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(p, 4, 0);
  lv_obj_set_style_border_color(p, COL_ACCENT, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *t = lv_label_create(p);
  lv_label_set_text_fmt(t, "Forget \"%s\"?", s_sel);
  lv_obj_set_style_text_font(t, FONT_L, 0);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *m = lv_label_create(p);
  lv_label_set_text(m, "The saved password is deleted.");
  lv_obj_align(m, LV_ALIGN_TOP_LEFT, 0, 44);
  lv_obj_t *c = ui_btn(p, "CANCEL", COL_NEUTRAL, 190, BTN_H, forget_cancel_cb, nullptr);
  lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *d = ui_btn(p, LV_SYMBOL_CLOSE " FORGET", COL_FAIL, 190, BTN_H, forget_go_cb, nullptr);
  lv_obj_align(d, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void scan_cb(lv_event_t *) { net_scan_start(); }

static void back_cb(lv_event_t *) { ui_show_login(); }

void ui_show_wifi() {
  lv_obj_t *scr = ui_screen_create();
  ui_header(scr, "WIFI NETWORKS");
  ui_set_current(UI_WIFI);
  s_sig[0] = '\0';
  s_wasScanning = false;
  // Start on the network in use (or the first saved one)
  net_saved_ssid(s_sel, sizeof(s_sel));

  s_list = lv_obj_create(scr);
  lv_obj_set_size(s_list, LIST_W, LIST_H);
  lv_obj_set_pos(s_list, 12, HEADER_H + 10);
  lv_obj_set_style_pad_all(s_list, 0, 0);
  lv_obj_set_style_pad_row(s_list, 0, 0);
  lv_obj_set_style_bg_color(s_list, COL_CARD, 0);
  lv_obj_set_style_border_color(s_list, COL_BORDER, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

  lv_obj_t *p = lv_obj_create(scr);
  lv_obj_set_size(p, PANEL_W, LIST_H);
  lv_obj_set_pos(p, PANEL_X, HEADER_H + 10);
  lv_obj_set_style_bg_color(p, COL_CARD, 0);
  lv_obj_set_style_border_color(p, COL_BORDER, 0);
  lv_obj_set_style_pad_all(p, 12, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);

  s_panel_name = lv_label_create(p);
  lv_label_set_long_mode(s_panel_name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_panel_name, PANEL_W - 26);
  lv_obj_set_style_text_font(s_panel_name, FONT_L, 0);
  lv_obj_set_pos(s_panel_name, 0, 0);

  s_panel_info = lv_label_create(p);
  lv_label_set_long_mode(s_panel_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_panel_info, PANEL_W - 26);
  lv_obj_set_pos(s_panel_info, 0, 32);

  s_pass_lbl = lv_label_create(p);
  lv_label_set_text(s_pass_lbl, "Password");
  lv_obj_set_style_text_color(s_pass_lbl, COL_MUTED, 0);
  lv_obj_set_pos(s_pass_lbl, 0, 78);

  s_pass_ta = lv_textarea_create(p);
  lv_textarea_set_one_line(s_pass_ta, true);
  lv_textarea_set_password_mode(s_pass_ta, true);
  lv_textarea_set_max_length(s_pass_ta, 63);
  lv_obj_set_width(s_pass_ta, PANEL_W - 26 - 58);
  lv_obj_set_pos(s_pass_ta, 0, 102);
  lv_obj_add_event_cb(s_pass_ta, pass_cb, LV_EVENT_ALL, nullptr);

  lv_obj_t *eye = ui_btn(p, LV_SYMBOL_EYE_CLOSE, COL_LABEL_BG, 50, 44, eye_cb, nullptr);
  lv_obj_clear_flag(eye, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  s_eye_lbl = lv_obj_get_child(eye, 0);
  lv_obj_align_to(eye, s_pass_ta, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

  s_btn_connect = ui_btn(p, LV_SYMBOL_WIFI " CONNECT", COL_OK, PANEL_W - 26, 52, connect_cb, nullptr);
  lv_obj_set_pos(s_btn_connect, 0, 166);
  s_btn_forget = ui_btn(p, LV_SYMBOL_CLOSE " FORGET", COL_FAIL, PANEL_W - 26, 52, forget_cb, nullptr);
  lv_obj_set_pos(s_btn_forget, 0, 228);

  s_status_lbl = lv_label_create(scr);
  lv_label_set_text(s_status_lbl, "");
  lv_obj_set_style_text_color(s_status_lbl, COL_MUTED, 0);
  lv_obj_set_pos(s_status_lbl, 14, HEADER_H + LIST_H + 18);

  lv_obj_t *f = ui_footer(scr);
  ui_btn(f, LV_SYMBOL_LEFT " BACK", COL_NEUTRAL, 200, BTN_H, back_cb, nullptr);
  s_btn_scan = ui_btn(f, LV_SYMBOL_REFRESH " SCAN", COL_PRIMARY, 200, BTN_H, scan_cb, nullptr);

  // Keyboard covers the footer and list bottom; the password field stays visible
  s_kb = ui_text_keyboard(scr, 240);

  net_scan_start();
  timer_cb(nullptr);
  s_timer = lv_timer_create(timer_cb, 500, nullptr);
  lv_obj_add_event_cb(scr, screen_deleted, LV_EVENT_DELETE, s_timer);

  ui_load(scr);
}
