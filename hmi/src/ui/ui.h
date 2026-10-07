#pragma once
#include <lvgl.h>
#include "ui_theme.h"

// Screen flow:
//   Login -> Model & Serials -> Test -> (Next batch) -> Model & Serials
//   Login -> Setup (view specs / operators / status)
//   Login -> Admin (password) -> WiFi / Manual relay / System
void ui_init();

enum UiScreen { UI_LOGIN, UI_MODEL, UI_TEST, UI_SETUP, UI_ADMIN };
void ui_set_current(UiScreen s);

// Newly synced specs/operators: may they be applied now, and refresh after
bool ui_can_apply_data();
void ui_data_changed();

void ui_show_login();
void ui_show_model();
void ui_show_test();
void ui_show_setup(void (*back)());
void ui_setup_refresh();
void ui_admin_prompt();
void ui_show_admin();

// --- shared helpers (ui_common.cpp) ---
lv_obj_t *ui_screen_create();
lv_obj_t *ui_text_keyboard(lv_obj_t *parent, lv_coord_t h);  // hidden, number row on top
void ui_load(lv_obj_t *scr);
lv_obj_t *ui_header(lv_obj_t *scr, const char *title);
lv_obj_t *ui_footer(lv_obj_t *scr);
lv_obj_t *ui_btn(lv_obj_t *parent, const char *txt, lv_color_t color, lv_coord_t w, lv_coord_t h,
                 lv_event_cb_t cb, void *user_data);
void ui_btn_set_enabled(lv_obj_t *btn, bool enabled);
void ui_toast(const char *title, const char *fmt, ...);
void ui_update_status();
