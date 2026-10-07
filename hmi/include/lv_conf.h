/**
 * LVGL 8.3 configuration for the CrowPanel 7" HMI.
 * Only the settings that differ from LVGL's defaults are listed here;
 * everything else falls back to lv_conf_internal.h.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Colour: RGB565, no byte swap (matches LovyanGFX rgb565_t) */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Memory: use the C heap (spills into PSRAM for large allocations) */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc

/* Tick from Arduino millis() */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_INDEV_DEF_READ_PERIOD 20

/* Fonts: the UI uses Bai Jamjuree (src/fonts/font_bai_*.c) */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(font_bai_16)
#define LV_FONT_DEFAULT &font_bai_16

/* Allow %f in lv_label_set_text_fmt() */
#define LV_SPRINTF_USE_FLOAT 1

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif /* LV_CONF_H */
