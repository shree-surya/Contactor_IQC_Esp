#include <Arduino.h>
#include "ui.h"
#include "../app_data.h"

static lv_obj_t *s_dd = nullptr;

static void login_cb(lv_event_t *) {
  uint16_t sel = lv_dropdown_get_selected(s_dd);
  if (sel == 0 || sel > (uint16_t)g_operatorCount) {
    ui_toast("Login", "Please select your name.");
    return;
  }
  strlcpy(g_app.operatorName, g_operators[sel - 1], sizeof(g_app.operatorName));
  ui_show_model();
}

static void setup_cb(lv_event_t *) { ui_show_setup(ui_show_login); }
static void admin_cb(lv_event_t *) { ui_admin_prompt(); }

void ui_show_login() {
  lv_obj_t *scr = ui_screen_create();
  ui_header(scr, "CONTACTOR IQC");
  ui_set_current(UI_LOGIN);

  lv_obj_t *card = lv_obj_create(scr);
  lv_obj_set_size(card, 560, 290);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(card, COL_CARD, 0);
  lv_obj_set_style_border_color(card, COL_BORDER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_side(card, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(card, 4, 0);
  lv_obj_set_style_border_color(card, COL_ACCENT, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, "Operator Login");
  lv_obj_set_style_text_font(t, FONT_L, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);

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
  lv_obj_set_width(s_dd, 460);
  lv_obj_set_style_text_font(s_dd, FONT_L, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(s_dd), FONT_L, 0);
  lv_obj_align(s_dd, LV_ALIGN_TOP_MID, 0, 60);

  lv_obj_t *b = ui_btn(card, LV_SYMBOL_OK "  LOGIN", COL_OK, 460, 70, login_cb, nullptr);
  lv_obj_set_style_text_font(lv_obj_get_child(b, 0), FONT_L, 0);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -8);

  lv_obj_t *f = ui_footer(scr);
  ui_btn(f, LV_SYMBOL_LIST "  SETUP", COL_NEUTRAL, 200, BTN_H, setup_cb, nullptr);
  ui_btn(f, LV_SYMBOL_SETTINGS "  ADMIN", COL_HEADER, 200, BTN_H, admin_cb, nullptr);

  ui_load(scr);
}
