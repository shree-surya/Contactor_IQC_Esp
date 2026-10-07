#include <Arduino.h>
#include "ui.h"
#include "../app_data.h"

static const lv_coord_t KB_H = 230;

static lv_obj_t *s_dd = nullptr;
static lv_obj_t *s_pw = nullptr;
static lv_obj_t *s_msg = nullptr;
static lv_obj_t *s_kb = nullptr;
static lv_obj_t *s_footer = nullptr;

static void show_msg(const char *txt, lv_color_t color) {
  lv_label_set_text(s_msg, txt);
  lv_obj_set_style_text_color(s_msg, color, 0);
}

// Operator and password are checked against the "Operators" tab of the Google
// Sheet (last synced copy, so login also works offline).
static void try_login() {
  int op = (int)lv_dropdown_get_selected(s_dd) - 1;
  if (op < 0 || op >= g_operatorCount) {
    show_msg("Select your name", COL_FAIL);
    return;
  }
  if (!app_data_check_password(op, lv_textarea_get_text(s_pw))) {
    lv_textarea_set_text(s_pw, "");
    show_msg("Wrong password", COL_FAIL);
    return;
  }
  strlcpy(g_app.operatorName, g_operators[op], sizeof(g_app.operatorName));
  ui_show_model();
}

static void login_cb(lv_event_t *) { try_login(); }

static void hide_kb() {
  lv_keyboard_set_textarea(s_kb, nullptr);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_state(s_pw, LV_STATE_FOCUSED);
}

static void pw_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(s_kb, s_pw);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_footer, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_READY) {  // keyboard tick = login
    hide_kb();
    try_login();
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL) {
    hide_kb();
  } else if (code == LV_EVENT_VALUE_CHANGED) {
    show_msg("", COL_MUTED);
  }
}

static void dd_cb(lv_event_t *) {
  lv_textarea_set_text(s_pw, "");
  show_msg("", COL_MUTED);
}

static void setup_cb(lv_event_t *) { ui_show_setup(ui_show_login); }
static void admin_cb(lv_event_t *) { ui_admin_prompt(); }

void ui_show_login() {
  lv_obj_t *scr = ui_screen_create();
  ui_header(scr, "CONTACTOR IQC");
  ui_set_current(UI_LOGIN);

  // Card sits high so the password field stays visible above the keyboard
  lv_obj_t *card = lv_obj_create(scr);
  lv_obj_set_size(card, 560, 300);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, HEADER_H + 12);
  lv_obj_set_style_bg_color(card, COL_CARD, 0);
  lv_obj_set_style_border_side(card, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(card, 4, 0);
  lv_obj_set_style_border_color(card, COL_ACCENT, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, "Operator Login");
  lv_obj_set_style_text_font(t, FONT_L, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

  // "-- Select --" followed by one line per operator
  static char opts[(NAME_MAX_LEN + 2) * (MAX_OPERATORS + 1)];
  strlcpy(opts, "-- Select operator --", sizeof(opts));
  uint16_t preselect = 0;
  for (int i = 0; i < g_operatorCount; i++) {
    strlcat(opts, "\n", sizeof(opts));
    strlcat(opts, g_operators[i], sizeof(opts));
    if (strcmp(g_operators[i], g_app.operatorName) == 0) preselect = i + 1;
  }

  s_dd = lv_dropdown_create(card);
  lv_dropdown_set_options(s_dd, opts);
  lv_dropdown_set_selected(s_dd, preselect);
  lv_obj_set_width(s_dd, 480);
  lv_obj_set_style_text_font(s_dd, FONT_M, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(s_dd), FONT_M, 0);
  lv_obj_align(s_dd, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_add_event_cb(s_dd, dd_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  s_pw = lv_textarea_create(card);
  lv_textarea_set_one_line(s_pw, true);
  lv_textarea_set_password_mode(s_pw, true);
  lv_textarea_set_max_length(s_pw, PASS_MAX_LEN);
  lv_textarea_set_placeholder_text(s_pw, "Password");
  lv_obj_set_width(s_pw, 480);
  lv_obj_set_style_text_font(s_pw, FONT_M, 0);
  lv_obj_align(s_pw, LV_ALIGN_TOP_MID, 0, 98);
  lv_obj_add_event_cb(s_pw, pw_event_cb, LV_EVENT_ALL, nullptr);

  s_msg = lv_label_create(card);
  lv_label_set_text(s_msg, "");
  lv_obj_align(s_msg, LV_ALIGN_TOP_MID, 0, 150);

  lv_obj_t *b = ui_btn(card, LV_SYMBOL_OK "  LOGIN", COL_OK, 480, 64, login_cb, nullptr);
  lv_obj_set_style_text_font(lv_obj_get_child(b, 0), FONT_L, 0);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);

  s_footer = ui_footer(scr);
  ui_btn(s_footer, LV_SYMBOL_LIST "  SETUP", COL_NEUTRAL, 200, BTN_H, setup_cb, nullptr);
  ui_btn(s_footer, LV_SYMBOL_SETTINGS "  ADMIN", COL_HEADER, 200, BTN_H, admin_cb, nullptr);

  s_kb = lv_keyboard_create(scr);
  lv_obj_set_size(s_kb, SCR_W, KB_H);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_font(s_kb, FONT_M, 0);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

  ui_load(scr);
}
