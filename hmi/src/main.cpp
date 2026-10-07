// Contactor IQC Rig - HMI firmware (Elecrow CrowPanel 7" ESP32-S3)
//
// Touch UI + Google Sheets link. The test rig is still simulated (hw_sim.cpp)
// until the ESP32-S3 sub-board is connected over I2C.

#include <Arduino.h>
#include <lvgl.h>

#include "config.h"
#include "app_data.h"
#include "display.h"
#include "gsheets.h"
#include "net.h"
#include "rig.h"
#include "ui/ui.h"

void setup() {
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

  // Every finished cycle becomes a row for the "Results" sheet
  ResultRow row;
  while (rig_pop_result(&row)) gsheets_enqueue(row);

  // Swap in freshly synced specs/operators only when no test is affected
  if (gsheets_has_new_data() && ui_can_apply_data()) {
    gsheets_apply_new_data();
    ui_data_changed();
  }

  lv_timer_handler();
  delay(5);
}
