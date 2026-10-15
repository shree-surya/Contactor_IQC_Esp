#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Sub-board wiring. Change pins and sensor addresses here only.
// ---------------------------------------------------------------------------

#define NUM_CH 5

// The ESP32-S3 has two I2C controllers:
//   I2C0 (bus A): all 5 INA219 sensors (this board is the master)
//   I2C1 (bus B): link to the HMI panel (this board is a slave, added later)
// INA219 jumpers give 4 addresses (0x40/41/44/45); the 5th uses A0 wired to
// SDA = 0x42 (see README).
#define BUS_A_SDA 8
#define BUS_A_SCL 9
#define BUS_B_SDA 17  // HMI link (not used yet)
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
    {0, 0x42},  // CH5: A0 wired to SDA (jumpers open)
};

// Shunt resistor on the INA219 module. Stock modules: R100 = 0.1 ohm ->
// max 3.2 A. For in-rush above ~3 A replace it with R050 (0.05 ohm, 6.4 A).
#define SHUNT_OHM 0.1f
