// Contactor IQC Rig - HMI firmware (Elecrow CrowPanel 7" ESP32-S3)
//
// Touch UI + Google Sheets link. The test rig is still simulated (hw_sim.cpp)
// until the ESP32-S3 sub-board is connected over I2C.

#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "app_data.h"
#include "display.h"
#include "gsheets.h"
#include "net.h"
#include "rig.h"
#include "ui/ui.h"

void setup() {
  // The RGB panel streams its frame buffer out of PSRAM all the time. By
  // default every malloc above 4 KB (WiFi and TLS buffers, JSON documents)
  // also lands in PSRAM, so an upload competes with the panel for the same
  // bus and the picture glitches. Prefer internal RAM up to 64 KB; it falls
  // back to PSRAM automatically when internal RAM is short.
  heap_caps_malloc_extmem_enable(64 * 1024);

  Serial.begin(115200);
  delay(200);
  Serial.println("\nContactor IQC HMI " FW_VERSION);

  app_data_init();   // built-in defaults
  gsheets_begin();   // overrides them with the cached Sheet data, starts the uploader
  display_init();
  net_begin();
  rig_begin();
  ui_init();
}

void loop() {
  rig_tick();
  gsheets_set_hold(rig_running());

  // Swap in freshly synced specs/operators only when no test is affected
  if (gsheets_has_new_data() && ui_can_apply_data()) {
    gsheets_apply_new_data();
    ui_data_changed();
  }

  lv_timer_handler();
  delay(5);
}
