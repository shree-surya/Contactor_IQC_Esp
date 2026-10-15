#pragma once
#include <Wire.h>

// Minimal INA219 driver: shunt voltage and bus voltage, no calibration
// register needed (current = shunt voltage / shunt resistance).
class Ina219 {
 public:
  bool begin(TwoWire *wire, uint8_t addr, float shuntOhm, bool fast = false);
  bool present() const { return ok_; }
  uint8_t addr() const { return addr_; }

  // fast = shunt only, 9-bit, a new value every 84 us (in-rush peaks; bus
  // voltage not updated). Normal = shunt + bus, 12-bit (lower noise).
  bool setFast(bool fast);

  bool readCurrent(float *amps);  // coil current, A (+ = into VIN+)
  bool readBusVolts(float *volts);  // voltage at VIN- (to GND), V
  bool overflow() const { return ovf_; }  // last bus read reported overflow

 private:
  bool write16(uint8_t reg, uint16_t val);
  bool read16(uint8_t reg, uint16_t *val);
  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0;
  float shunt_ = 0.1f;
  bool ok_ = false;
  bool ovf_ = false;
};
