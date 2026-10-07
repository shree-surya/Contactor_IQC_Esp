#pragma once
#include <lvgl.h>

// ---------------------------------------------------------------------------
// All colours, fonts and common sizes for the UI live here.
// Change the look of the whole application from this one file.
// ---------------------------------------------------------------------------

#define COL_BG        lv_color_hex(0xEEF1F5)
#define COL_CARD      lv_color_hex(0xFFFFFF)
#define COL_HEADER    lv_color_hex(0x1F2A44)
#define COL_TEXT      lv_color_hex(0x1A1A1A)
#define COL_MUTED     lv_color_hex(0x6B7280)
#define COL_PRIMARY   lv_color_hex(0x1565C0)
#define COL_OK        lv_color_hex(0x2E7D32)
#define COL_FAIL      lv_color_hex(0xC62828)
#define COL_WARN      lv_color_hex(0xF9A825)
#define COL_NEUTRAL   lv_color_hex(0x546E7A)

// Light cell backgrounds for the result table
#define COL_PASS_BG   lv_color_hex(0xDFF5E1)
#define COL_FAIL_BG   lv_color_hex(0xFBE0E0)
#define COL_RUN_BG    lv_color_hex(0xFFF4CC)
#define COL_WAIT_BG   lv_color_hex(0xE3EEFB)
#define COL_LABEL_BG  lv_color_hex(0xF3F4F6)

#define FONT_S   (&lv_font_montserrat_16)
#define FONT_M   (&lv_font_montserrat_20)
#define FONT_L   (&lv_font_montserrat_24)
#define FONT_XL  (&lv_font_montserrat_32)

#define SCR_W     800
#define SCR_H     480
#define HEADER_H  50
#define FOOTER_H  70
#define BTN_H     56
