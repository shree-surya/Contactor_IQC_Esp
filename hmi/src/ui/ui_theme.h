#pragma once
#include <lvgl.h>

// ---------------------------------------------------------------------------
// All colours, fonts and common sizes for the UI live here.
// Change the look of the whole application from this one file.
//
// Palette: near-black base with cyan / orange / lime accents.
// Red is added for FAIL only, so a failure is never mistaken for "running".
// ---------------------------------------------------------------------------

// Base
#define COL_BG        lv_color_hex(0x0F1115)  // near-black screen background
#define COL_CARD      lv_color_hex(0x181B22)  // panels, table cells
#define COL_HEADER    lv_color_hex(0x161A21)  // top bar
#define COL_LABEL_BG  lv_color_hex(0x1C2028)  // table label column
#define COL_BORDER    lv_color_hex(0x2C313C)
#define COL_TEXT      lv_color_hex(0xE8ECF1)
#define COL_MUTED     lv_color_hex(0x8A93A3)
#define COL_DARK_TEXT lv_color_hex(0x0F1115)  // text on bright buttons

// Accents
#define COL_CYAN      lv_color_hex(0x35D0FF)
#define COL_ORANGE    lv_color_hex(0xFF8A3D)
#define COL_LIME      lv_color_hex(0xB6F24A)
#define COL_RED       lv_color_hex(0xFF4D5E)

// Roles
#define COL_PRIMARY   COL_CYAN
#define COL_OK        COL_LIME
#define COL_FAIL      COL_RED
#define COL_WARN      COL_ORANGE
#define COL_NEUTRAL   lv_color_hex(0x2C313C)  // secondary buttons
#define COL_ABORT     lv_color_hex(0x6B4A3A)

// Tinted cell backgrounds for the result table
#define COL_PASS_BG   lv_color_hex(0x232E17)
#define COL_FAIL_BG   lv_color_hex(0x3A1A20)
#define COL_RUN_BG    lv_color_hex(0x3A2616)
#define COL_WAIT_BG   lv_color_hex(0x10303D)

// Fonts: Bai Jamjuree SemiBold (src/fonts), with LVGL symbols merged in
LV_FONT_DECLARE(font_bai_16);
LV_FONT_DECLARE(font_bai_20);
LV_FONT_DECLARE(font_bai_24);
LV_FONT_DECLARE(font_bai_32);
#define FONT_S   (&font_bai_16)
#define FONT_M   (&font_bai_20)
#define FONT_L   (&font_bai_24)
#define FONT_XL  (&font_bai_32)

#define SCR_W     800
#define SCR_H     480
#define HEADER_H  50
#define FOOTER_H  70
#define BTN_H     56
