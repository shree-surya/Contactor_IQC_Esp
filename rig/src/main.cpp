// Contactor IQC Rig - sub-board, step 1: INA219 bring-up
//
// Prints all 5 channels (current + bus voltage) 5 times a second.
// Serial Monitor commands:
//   s        scan both I2C buses and list the addresses found
//   1 .. 5   peak test: sample that channel as fast as possible for 1 s and
//            print peak, average and samples/s (switch the coil on during it)
//   p        pause / resume the live table

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "ina219.h"

static TwoWire &busA = Wire;
static TwoWire &busB = Wire1;
static bool s_useB = false;  // a sensor is configured on bus B
static Ina219 s_ina[NUM_CH];
static bool s_paused = false;

static TwoWire *bus_of(int ch) { return SENSORS[ch].bus == 0 ? &busA : &busB; }

static void scan_bus(TwoWire &w, const char *name) {
  Serial.printf("%s:", name);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      n++;
    }
  }
  Serial.println(n ? "" : " nothing found - check wiring / 3.3 V");
}

static void init_sensors() {
  for (int ch = 0; ch < NUM_CH; ch++) {
    bool ok = s_ina[ch].begin(bus_of(ch), SENSORS[ch].addr, SHUNT_OHM);
    Serial.printf("CH%d  bus %c  0x%02X  %s\n", ch + 1, SENSORS[ch].bus ? 'B' : 'A', SENSORS[ch].addr,
                  ok ? "OK" : "NOT FOUND");
  }
}

static void print_table() {
  static uint32_t lines = 0;
  if (lines++ % 20 == 0) {
    Serial.println();
    Serial.println("      CH1            CH2            CH3            CH4            CH5");
  }
  for (int ch = 0; ch < NUM_CH; ch++) {
    float a, v;
    if (s_ina[ch].present() && s_ina[ch].readCurrent(&a) && s_ina[ch].readBusVolts(&v)) {
      Serial.printf("%6.3fA %5.2fV%s", a, v, s_ina[ch].overflow() ? "!" : " ");
    } else {
      Serial.print("   --  --      ");
    }
  }
  Serial.println();
}

// Samples one channel for 1 s at the fastest rate (9-bit shunt ADC)
static void peak_test(int ch) {
  Ina219 &s = s_ina[ch];
  if (!s.present()) {
    Serial.printf("CH%d not found\n", ch + 1);
    return;
  }
  Serial.printf("CH%d peak test: switch the coil ON now (1 s)...\n", ch + 1);
  s.setFast(true);
  float peak = 0, sum = 0, a;
  uint32_t n = 0, t0 = millis();
  while (millis() - t0 < 1000) {
    if (s.readCurrent(&a)) {
      if (a > peak) peak = a;
      sum += a;
      n++;
    }
  }
  s.setFast(false);
  Serial.printf("CH%d  peak %.3f A   avg %.3f A   %lu samples/s%s\n", ch + 1, peak, n ? sum / n : 0,
                (unsigned long)n, peak > 0.98f * (0.32f / SHUNT_OHM) ? "   OVER RANGE - use a smaller shunt" : "");
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // time to open the Serial Monitor
  Serial.println("\nContactor IQC rig - INA219 test");

  busA.begin(BUS_A_SDA, BUS_A_SCL, I2C_HZ);
  scan_bus(busA, "Bus A (SDA 8, SCL 9)  ");
  for (int ch = 0; ch < NUM_CH; ch++) s_useB |= SENSORS[ch].bus == 1;
  if (s_useB) {
    busB.begin(BUS_B_SDA, BUS_B_SCL, I2C_HZ);
    scan_bus(busB, "Bus B (SDA 17, SCL 18)");
  }
  init_sensors();
  Serial.println("Commands: s = scan, 1..5 = peak test, p = pause");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's') {
      scan_bus(busA, "Bus A");
      if (s_useB) scan_bus(busB, "Bus B");
      init_sensors();
    } else if (c >= '1' && c <= '5') {
      peak_test(c - '1');
    } else if (c == 'p') {
      s_paused = !s_paused;
    }
  }
  static uint32_t last = 0;
  if (!s_paused && millis() - last >= 200) {
    last = millis();
    print_table();
  }
}
