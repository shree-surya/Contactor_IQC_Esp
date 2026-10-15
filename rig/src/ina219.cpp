#include "ina219.h"

// Registers
static const uint8_t REG_CONFIG = 0x00;
static const uint8_t REG_SHUNT = 0x01;
static const uint8_t REG_BUS = 0x02;

// Config: 16 V bus range, PGA /8 (+-320 mV shunt), bus ADC 12-bit,
// continuous shunt + bus conversion. Shunt ADC set by setFast().
static const uint16_t CFG_BASE = (0 << 13)    // BRNG 16 V (coils are 12 V)
                                 | (3 << 11)  // PG /8, +-320 mV
                                 | (3 << 7)   // BADC 12-bit, 532 us
                                 | 7;         // shunt + bus, continuous
static const uint16_t SADC_12BIT = 3 << 3;    // 532 us
// Fast: shunt only, 9-bit, continuous -> a new reading every 84 us
static const uint16_t CFG_FAST = (0 << 13) | (3 << 11) | (0 << 3) | 5;

bool Ina219::write16(uint8_t reg, uint16_t val) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write((uint8_t)(val >> 8));
  wire_->write((uint8_t)(val & 0xFF));
  return wire_->endTransmission() == 0;
}

bool Ina219::read16(uint8_t reg, uint16_t *val) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(addr_, (uint8_t)2) != 2) return false;
  uint16_t hi = wire_->read();
  uint16_t lo = wire_->read();
  *val = (hi << 8) | lo;
  return true;
}

bool Ina219::begin(TwoWire *wire, uint8_t addr, float shuntOhm, bool fast) {
  wire_ = wire;
  addr_ = addr;
  shunt_ = shuntOhm;
  ok_ = write16(REG_CONFIG, 0x8000);  // reset
  delay(1);
  ok_ = ok_ && setFast(fast);
  return ok_;
}

bool Ina219::setFast(bool fast) {
  return write16(REG_CONFIG, fast ? CFG_FAST : (CFG_BASE | SADC_12BIT));
}

bool Ina219::readCurrent(float *amps) {
  uint16_t raw;
  if (!read16(REG_SHUNT, &raw)) return false;
  float shuntVolts = (int16_t)raw * 10e-6f;  // LSB 10 uV
  *amps = shuntVolts / shunt_;
  return true;
}

bool Ina219::readBusVolts(float *volts) {
  uint16_t raw;
  if (!read16(REG_BUS, &raw)) return false;
  ovf_ = raw & 0x01;
  *volts = (raw >> 3) * 0.004f;  // LSB 4 mV
  return true;
}
