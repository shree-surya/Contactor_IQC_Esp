#define LGFX_USE_V1
#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lvgl.h>

#include "config.h"
#include "display.h"

static const uint16_t SCREEN_W = 800;
static const uint16_t SCREEN_H = 480;

// Panel timing and pin map for the CrowPanel 7" (from Elecrow's reference code).
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB _bus;
  lgfx::Panel_RGB _panel;
  lgfx::Light_PWM _light;
  lgfx::Touch_GT911 _touch;

  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width = SCREEN_W;
      cfg.memory_height = SCREEN_H;
      cfg.panel_width = SCREEN_W;
      cfg.panel_height = SCREEN_H;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      cfg.pin_d0 = GPIO_NUM_15;   // B0
      cfg.pin_d1 = GPIO_NUM_7;    // B1
      cfg.pin_d2 = GPIO_NUM_6;    // B2
      cfg.pin_d3 = GPIO_NUM_5;    // B3
      cfg.pin_d4 = GPIO_NUM_4;    // B4
      cfg.pin_d5 = GPIO_NUM_9;    // G0
      cfg.pin_d6 = GPIO_NUM_46;   // G1
      cfg.pin_d7 = GPIO_NUM_3;    // G2
      cfg.pin_d8 = GPIO_NUM_8;    // G3
      cfg.pin_d9 = GPIO_NUM_16;   // G4
      cfg.pin_d10 = GPIO_NUM_1;   // G5
      cfg.pin_d11 = GPIO_NUM_14;  // R0
      cfg.pin_d12 = GPIO_NUM_21;  // R1
      cfg.pin_d13 = GPIO_NUM_47;  // R2
      cfg.pin_d14 = GPIO_NUM_48;  // R3
      cfg.pin_d15 = GPIO_NUM_45;  // R4
      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync = GPIO_NUM_40;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0;
      cfg.freq_write = 15000000;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch = 40;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch = 13;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);
    {
      auto cfg = _light.config();
      cfg.pin_bl = GPIO_NUM_2;
      _light.config(cfg);
    }
    _panel.light(&_light);
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = SCREEN_W - 1;
      cfg.y_min = 0;
      cfg.y_max = SCREEN_H - 1;
      cfg.pin_int = -1;
      cfg.pin_rst = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;  // I2C peripheral 1 (Wire uses 0)
      cfg.pin_sda = (gpio_num_t)PIN_I2C_SDA;
      cfg.pin_scl = (gpio_num_t)PIN_I2C_SCL;
      cfg.freq = 400000;
      cfg.i2c_addr = 0x5D;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX tft;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

static bool pca9557_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCA9557_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// On the V3.0 board the GT911 reset (IO0) and INT (IO1) lines sit on a PCA9557
// I/O expander. Holding INT low while releasing reset fixes the GT911 at
// address 0x5D. Only IO0/IO1 are touched; the other expander pins are left alone.
static void touch_reset_v3() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  if (!pca9557_write(0x02, 0x00)) {  // polarity: no inversion
    Serial.println("[display] PCA9557 not found, skipping touch reset");
    Wire.end();
    return;
  }
  pca9557_write(0x01, 0x00);  // IO0 (RST) low, IO1 (INT) low
  pca9557_write(0x03, 0xFC);  // IO0 + IO1 as outputs
  delay(20);
  pca9557_write(0x01, 0x01);  // release reset, INT still low
  delay(100);
  pca9557_write(0x03, 0xFE);  // INT back to input
  Wire.end();
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&px->full);
  lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint16_t x, y;
  if (tft.getTouch(&x, &y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void display_init() {
  touch_reset_v3();

  tft.begin();
  tft.fillScreen(TFT_BLACK);
  tft.setBrightness(255);

  lv_init();

  const size_t buf_px = SCREEN_W * SCREEN_H / 10;
  lv_color_t *buf = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t),
                                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) {
    buf = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  }
  lv_disp_draw_buf_init(&s_draw_buf, buf, NULL, buf_px);

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res = SCREEN_W;
  s_disp_drv.ver_res = SCREEN_H;
  s_disp_drv.flush_cb = flush_cb;
  s_disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);

  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = touch_cb;
  lv_indev_drv_register(&s_indev_drv);
}
