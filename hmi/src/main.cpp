// Contactor IQC Rig - HMI firmware (Elecrow CrowPanel 7" ESP32-S3)
//
// This build contains the full touch UI with a simulated test rig, so the
// screens and the test flow can be checked before the sub-board is built.
// Google Sheets and the I2C link to the sub-board come in the next steps.

#include <Arduino.h>
#include <lvgl.h>

#include "config.h"
#include "app_data.h"
#include "display.h"
#include "net.h"
#include "rig.h"
#include "ui/ui.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nContactor IQC HMI " FW_VERSION);

  app_data_init();
  display_init();
  net_begin();
  rig_begin();
  ui_init();
}

void loop() {
  rig_tick();
  lv_timer_handler();
  delay(5);
}
