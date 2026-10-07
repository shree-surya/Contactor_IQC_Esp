#include <Arduino.h>
#include <stdarg.h>
#include "ui.h"
#include "../net.h"
#include "../rig.h"
#include "../gsheets.h"

static lv_obj_t *s_status_lbl = nullptr;
static UiScreen s_current = UI_LOGIN;

void ui_set_current(UiScreen s) { s_current = s; }

bool ui_can_apply_data() { return s_current != UI_TEST; }

void ui_data_changed() {
  switch (s_current) {
    case UI_LOGIN: ui_show_login(); break;
    case UI_MODEL: ui_show_model(); break;
    case UI_SETUP: ui_setup_refresh(); break;
    default: break;
  }
}

static void status_lbl_deleted(lv_event_t *e) {
  if (lv_event_get_target(e) == s_status_lbl) s_status_lbl = nullptr;
}

static void status_timer_cb(lv_timer_t *) { ui_update_status(); }

// Text keyboard with a number row on top (used for passwords and WiFi).
// LVGL keeps these maps globally, so every text keyboard picks them up.
#define K(w) (lv_btnmatrix_ctrl_t)(w)
#define KS(w) (lv_btnmatrix_ctrl_t)(LV_KEYBOARD_CTRL_BTN_FLAGS | (w))
static const char *KB_LOWER[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
  "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
  "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
  "1#", "z", "x", "c", "v", "b", "n", "m", ".", "-", "_", "\n",
  LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
static const char *KB_UPPER[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
  "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
  "1#", "Z", "X", "C", "V", "B", "N", "M", ".", "-", "_", "\n",
  LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
static const lv_btnmatrix_ctrl_t KB_TEXT_CTRL[] = {
  K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), KS(6),
  K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4),
  KS(6), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4),
  KS(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4), K(4),
  KS(3), KS(2), K(7), KS(2), KS(3)};

void ui_init() {
  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t *th = lv_theme_default_init(disp, COL_TEXT, COL_ACCENT, false, FONT_S);
  lv_disp_set_theme(disp, th);
  lv_timer_create(status_timer_cb, 500, nullptr);
  ui_show_login();
}

lv_obj_t *ui_text_keyboard(lv_obj_t *parent, lv_coord_t h) {
  lv_obj_t *kb = lv_keyboard_create(parent);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_TEXT_CTRL);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_TEXT_CTRL);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_set_size(kb, SCR_W, h);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_font(kb, FONT_M, 0);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  return kb;
}

lv_obj_t *ui_screen_create() {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_text_color(scr, COL_TEXT, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

// Loads a new screen and deletes the previous one once the current event has
// finished (deleting it synchronously from a button callback is unsafe).
void ui_load(lv_obj_t *scr) {
  lv_obj_t *old = lv_scr_act();
  lv_scr_load(scr);
  if (old && old != scr) lv_obj_del_async(old);
}

lv_obj_t *ui_header(lv_obj_t *scr, const char *title) {
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, SCR_W, HEADER_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, COL_HEADER, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 2, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(bar, COL_ACCENT, 0);
  lv_obj_set_style_pad_hor(bar, 14, 0);
  lv_obj_set_style_pad_ver(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(bar);
  lv_label_set_text(t, title);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 570);
  lv_obj_set_style_text_font(t, FONT_M, 0);
  lv_obj_set_style_text_color(t, COL_ON_DARK, 0);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);

  s_status_lbl = lv_label_create(bar);
  lv_obj_set_style_text_font(s_status_lbl, FONT_S, 0);
  lv_obj_set_style_text_color(s_status_lbl, COL_ACCENT, 0);
  lv_obj_align(s_status_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(s_status_lbl, status_lbl_deleted, LV_EVENT_DELETE, nullptr);
  ui_update_status();
  return bar;
}

lv_obj_t *ui_footer(lv_obj_t *scr) {
  lv_obj_t *f = lv_obj_create(scr);
  lv_obj_set_size(f, SCR_W, FOOTER_H);
  lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(f, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(f, 0, 0);
  lv_obj_set_style_pad_hor(f, 12, 0);
  lv_obj_set_style_pad_ver(f, 7, 0);
  lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(f, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return f;
}

lv_obj_t *ui_btn(lv_obj_t *parent, const char *txt, lv_color_t color, lv_coord_t w, lv_coord_t h,
                 lv_event_cb_t cb, void *user_data) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, color, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, FONT_M, 0);
  // Dark text on bright accent buttons, light text on dark ones
  lv_obj_set_style_text_color(l, lv_color_brightness(color) > 140 ? COL_DARK_TEXT : COL_ON_DARK, 0);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
  return b;
}

void ui_btn_set_enabled(lv_obj_t *btn, bool enabled) {
  if (enabled) lv_obj_clear_state(btn, LV_STATE_DISABLED);
  else lv_obj_add_state(btn, LV_STATE_DISABLED);
}

void ui_toast(const char *title, const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  lv_obj_t *mb = lv_msgbox_create(nullptr, title, buf, nullptr, true);
  lv_obj_set_width(mb, 460);
  lv_obj_set_style_text_font(mb, FONT_M, 0);
  lv_obj_center(mb);
}

// WiFi indicator on every screen: green = connected, blinking yellow =
// connecting, grey = no WiFi configured
void ui_update_status() {
  if (!s_status_lbl) return;
  static bool blink = false;
  blink = !blink;
  switch (net_state()) {
    case NET_CONNECTED:
      lv_label_set_text(s_status_lbl, LV_SYMBOL_WIFI "  WiFi OK");
      lv_obj_set_style_text_color(s_status_lbl, COL_LED_OK, 0);
      lv_obj_set_style_text_opa(s_status_lbl, LV_OPA_COVER, 0);
      break;
    case NET_CONNECTING:
      lv_label_set_text(s_status_lbl, LV_SYMBOL_WIFI "  Connecting...");
      lv_obj_set_style_text_color(s_status_lbl, COL_ACCENT, 0);
      lv_obj_set_style_text_opa(s_status_lbl, blink ? LV_OPA_COVER : LV_OPA_40, 0);
      break;
    default:
      lv_label_set_text(s_status_lbl, LV_SYMBOL_WIFI "  No WiFi");
      lv_obj_set_style_text_color(s_status_lbl, COL_MUTED, 0);
      lv_obj_set_style_text_opa(s_status_lbl, LV_OPA_COVER, 0);
      break;
  }
}
