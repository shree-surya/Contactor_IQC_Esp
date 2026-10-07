#pragma once
#include <lvgl.h>

// ---------------------------------------------------------------------------
// All colours, fonts and common sizes for the UI live here.
// Change the look of the whole application from this one file.
//
// Light theme based on the reference design: black bars with yellow
// (#FFCC00) accents, white / light-grey surfaces, grey secondary text.
// Green and red are added for PASS / FAIL.
// ---------------------------------------------------------------------------

// Base (light)
#define COL_BG        lv_color_hex(0xF2F2F2)  // screen background
#define COL_CARD      lv_color_hex(0xFFFFFF)  // panels, table cells
#define COL_ROW_ALT   lv_color_hex(0xF7F7F7)  // alternate table rows
#define COL_HEADER    lv_color_hex(0x000000)  // top bar, table header (reference: --bg-deep)
#define COL_LABEL_BG  lv_color_hex(0xEDEDED)  // table label column
#define COL_BORDER    lv_color_hex(0xDADADA)
#define COL_TEXT      lv_color_hex(0x1E1E1E)  // reference: --bg used as text on light
#define COL_MUTED     lv_color_hex(0x808080)  // reference: --muted
#define COL_SOFT      lv_color_hex(0xA0A0A0)  // reference: --fg2
#define COL_DARK_TEXT lv_color_hex(0x000000)  // text on bright buttons
#define COL_ON_DARK   lv_color_hex(0xFFFFFF)  // text on black bars

// Accents
#define COL_ACCENT    lv_color_hex(0xFFCC00)  // reference: --accent
#define COL_GREEN     lv_color_hex(0x1F9D55)
#define COL_RED       lv_color_hex(0xE5484D)
#define COL_WARN_TXT  lv_color_hex(0xFF6B4A)  // reference: .warn
#define COL_BUSY_TXT  lv_color_hex(0xB08900)  // "in progress" text on light backgrounds
#define COL_LED_OK    lv_color_hex(0x3DDC84)  // status icons on the black bar

// Roles
#define COL_PRIMARY   COL_ACCENT
#define COL_OK        COL_GREEN
#define COL_FAIL      COL_RED
#define COL_WARN      COL_ACCENT
#define COL_NEUTRAL   lv_color_hex(0x262626)  // secondary buttons (reference: --row-odd)
#define COL_ABORT     lv_color_hex(0x6D4C41)

// Tinted cell backgrounds for the result table
#define COL_PASS_BG   lv_color_hex(0xE3F5EA)
#define COL_FAIL_BG   lv_color_hex(0xFDE4E5)
#define COL_RUN_BG    lv_color_hex(0xFFF5CC)
#define COL_LIVE_BG   lv_color_hex(0xFFF8DB)

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
