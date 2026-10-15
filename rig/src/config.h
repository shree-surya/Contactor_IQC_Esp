#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Sub-board wiring. Change pins and sensor addresses here only.
// ---------------------------------------------------------------------------

#define NUM_CH 5

// INA219 modules only have two address jumpers (A0, A1) -> 4 addresses, so
// 5 sensors use both I2C controllers of the ESP32-S3:
//   Bus A (I2C0): CH1..CH4 at 0x40, 0x41, 0x44, 0x45
//   Bus B (I2C1): CH5 at 0x40 (no jumpers)
#define BUS_A_SDA 8
#define BUS_A_SCL 9
#define BUS_B_SDA 17
#define BUS_B_SCL 18
#define I2C_HZ 400000

struct SensorCfg {
  uint8_t bus;   // 0 = bus A, 1 = bus B
  uint8_t addr;  // 7-bit I2C address
};

static const SensorCfg SENSORS[NUM_CH] = {
    {0, 0x40},  // CH1: no jumper
    {0, 0x41},  // CH2: A0 bridged
    {0, 0x44},  // CH3: A1 bridged
    {0, 0x45},  // CH4: A0 + A1 bridged
    {1, 0x40},  // CH5: no jumper, on bus B
};

// Shunt resistor on the INA219 module. Stock modules: R100 = 0.1 ohm ->
// max 3.2 A. For in-rush above ~3 A replace it with R050 (0.05 ohm, 6.4 A).
#define SHUNT_OHM 0.1f
