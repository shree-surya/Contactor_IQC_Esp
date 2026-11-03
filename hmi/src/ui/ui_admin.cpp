#include <Arduino.h>
#include "ui.h"
#include "../app_data.h"
#include "../rig.h"

// ---------------------------------------------------------------------------
// Password prompt (overlay on the top layer)
// ---------------------------------------------------------------------------

static lv_obj_t *s_pw_overlay = nullptr;
static lv_obj_t *s_pw_msg = nullptr;
static void (*s_pw_ok)() = nullptr;

static void pw_close() {
  if (s_pw_overlay) {
    lv_obj_del_async(s_pw_overlay);
    s_pw_overlay = nullptr;
  }
}

static void pw_ta_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  if (code == LV_EVENT_READY) {
    if (strcmp(lv_textarea_get_text(ta), ADMIN_PASSWORD) == 0) {
      pw_close();
      if (s_pw_ok) s_pw_ok();
    } else {
      lv_textarea_set_text(ta, "");
      lv_label_set_text(s_pw_msg, "Wrong password");
      lv_obj_set_style_text_color(s_pw_msg, COL_FAIL, 0);
    }
  } else if (code == LV_EVENT_CANCEL) {
    pw_close();
  }
}

static void pw_cancel_cb(lv_event_t *) { pw_close(); }

void ui_admin_prompt(void (*on_ok)()) {
  if (s_pw_overlay) return;
  s_pw_ok = on_ok;
  s_pw_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_pw_overlay, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(s_pw_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_pw_overlay, LV_OPA_50, 0);
  lv_obj_set_style_radius(s_pw_overlay, 0, 0);
  lv_obj_set_style_border_width(s_pw_overlay, 0, 0);
  lv_obj_clear_flag(s_pw_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *panel = lv_obj_create(s_pw_overlay);
  lv_obj_set_size(panel, 420, 180);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  s_pw_msg = lv_label_create(panel);
  lv_label_set_text(s_pw_msg, "Admin password");
  lv_obj_set_style_text_font(s_pw_msg, FONT_M, 0);
  lv_obj_align(s_pw_msg, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *ta = lv_textarea_create(panel);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_password_mode(ta, true);
  lv_textarea_set_max_length(ta, 12);
  lv_obj_set_width(ta, 250);
  lv_obj_set_style_text_font(ta, FONT_L, 0);
  lv_obj_align(ta, LV_ALIGN_LEFT_MID, 0, 18);
  lv_obj_add_event_cb(ta, pw_ta_cb, LV_EVENT_ALL, nullptr);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);

  lv_obj_t *c = ui_btn(panel, "CANCEL", COL_NEUTRAL, 120, 50, pw_cancel_cb, nullptr);
  lv_obj_align(c, LV_ALIGN_RIGHT_MID, 0, 18);

  lv_obj_t *kb = lv_keyboard_create(s_pw_overlay);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_set_size(kb, SCR_W, 260);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_text_font(kb, FONT_L, 0);
  lv_keyboard_set_textarea(kb, ta);
}

// ---------------------------------------------------------------------------
// Admin screen
// ---------------------------------------------------------------------------

static lv_obj_t *s_relay_lbl[NUM_CH];
static lv_obj_t *s_sys_lbl = nullptr;
static lv_timer_t *s_timer = nullptr;

static void relay_cb(lv_event_t *e) {
  intptr_t code = (intptr_t)lv_event_get_user_data(e);
  int ch = code / 2;
  bool on = code % 2;
  if (!rig_manual_relay(ch, on)) ui_toast("Manual", "Not allowed while a test is running.");
}

static void all_off_cb(lv_event_t *) {
  for (int ch = 0; ch < NUM_CH; ch++) rig_manual_relay(ch, false);
}

static void reboot_cb(lv_event_t *) { ESP.restart(); }

static void back_cb(lv_event_t *) {
  all_off_cb(nullptr);
  ui_show_login();
}

static void timer_cb(lv_timer_t *) {
  for (int ch = 0; ch < NUM_CH; ch++) {
    lv_label_set_text_fmt(s_relay_lbl[ch], "Relay %s   Contact %s   %.3f A", rig_relay_state(ch) ? "ON " : "OFF",
                          rig_contact_closed(ch) ? "CLOSE" : "OPEN", rig_current(ch));
  }

  if (s_sys_lbl) {
    lv_label_set_text_fmt(s_sys_lbl,
                          "Firmware:      " FW_VERSION "\n"
                          "Uptime:        %lu s\n"
                          "Free heap:     %u bytes\n"
                          "Free PSRAM:    %u bytes\n"
                          "Test rig:      %s",
                          (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
                          rig_is_simulated() ? "SIMULATION" : rig_hw_ok() ? "rig board connected" : "rig board NOT connected");
  }
}

// Deletes the refresh timer that belongs to the screen being deleted
static void screen_deleted(lv_event_t *e) {
  lv_timer_t *t = (lv_timer_t *)lv_event_get_user_data(e);
  lv_timer_del(t);
  if (s_timer == t) {
    s_timer = nullptr;
    s_sys_lbl = nullptr;
  }
}

static void build_manual_tab(lv_obj_t *tab) {
  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab, 6, 0);

  lv_obj_t *note = lv_label_create(tab);
  lv_label_set_text(note, rig_is_simulated() ? "SIMULATION - no rig board. Values are simulated."
                                             : "Manual relay test. Only one relay at a time is recommended.");
  lv_obj_set_style_text_color(note, COL_MUTED, 0);

  for (int ch = 0; ch < NUM_CH; ch++) {
    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_set_size(row, lv_pct(100), 48);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *n = lv_label_create(row);
    lv_label_set_text_fmt(n, "CH%d", ch + 1);
    lv_obj_set_style_text_font(n, FONT_M, 0);
    lv_obj_set_width(n, 60);
    ui_btn(row, "ON", COL_OK, 90, 42, relay_cb, (void *)(intptr_t)(ch * 2 + 1));
    ui_btn(row, "OFF", COL_FAIL, 90, 42, relay_cb, (void *)(intptr_t)(ch * 2));
    s_relay_lbl[ch] = lv_label_create(row);
    lv_obj_set_style_text_font(s_relay_lbl[ch], FONT_M, 0);
  }
  ui_btn(tab, "ALL RELAYS OFF", COL_FAIL, 240, 46, all_off_cb, nullptr);
}

static void build_system_tab(lv_obj_t *tab) {
  s_sys_lbl = lv_label_create(tab);
  lv_obj_set_style_text_font(s_sys_lbl, FONT_M, 0);
  lv_obj_set_style_text_line_space(s_sys_lbl, 6, 0);
  lv_obj_t *b = ui_btn(tab, LV_SYMBOL_POWER " REBOOT", COL_FAIL, 200, 50, reboot_cb, nullptr);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void ui_show_admin() {
  lv_obj_t *scr = ui_screen_create();
  ui_header(scr, "ADMIN");
  ui_set_current(UI_ADMIN);

  lv_obj_t *tv = lv_tabview_create(scr, LV_DIR_TOP, 46);
  lv_obj_set_size(tv, SCR_W, SCR_H - HEADER_H - FOOTER_H);
  lv_obj_set_pos(tv, 0, HEADER_H);
  lv_obj_set_style_text_font(lv_tabview_get_tab_btns(tv), FONT_M, 0);

  build_manual_tab(lv_tabview_add_tab(tv, "Manual test"));
  build_system_tab(lv_tabview_add_tab(tv, "System"));

  lv_obj_t *f = ui_footer(scr);
  ui_btn(f, LV_SYMBOL_LEFT " BACK", COL_NEUTRAL, 200, BTN_H, back_cb, nullptr);

  timer_cb(nullptr);
  s_timer = lv_timer_create(timer_cb, 300, nullptr);
  lv_obj_add_event_cb(scr, screen_deleted, LV_EVENT_DELETE, s_timer);

  ui_load(scr);
}
