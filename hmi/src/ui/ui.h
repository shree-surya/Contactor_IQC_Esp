#pragma once
#include <lvgl.h>
#include "ui_theme.h"

// Screen flow:
//   Login -> Model & Serials -> Test -> (Next batch) -> Model & Serials
//   Login -> Setup (view specs / operators / status)
//   Login -> Admin (password) -> WiFi / Manual relay / System
void ui_init();

void ui_show_login();
void ui_show_model();
void ui_show_test();
void ui_show_setup(void (*back)());
void ui_admin_prompt();
void ui_show_admin();

// --- shared helpers (ui_common.cpp) ---
lv_obj_t *ui_screen_create();
void ui_load(lv_obj_t *scr);
lv_obj_t *ui_header(lv_obj_t *scr, const char *title);
lv_obj_t *ui_footer(lv_obj_t *scr);
lv_obj_t *ui_btn(lv_obj_t *parent, const char *txt, lv_color_t color, lv_coord_t w, lv_coord_t h,
                 lv_event_cb_t cb, void *user_data);
void ui_btn_set_enabled(lv_obj_t *btn, bool enabled);
void ui_toast(const char *title, const char *fmt, ...);
void ui_update_status();
