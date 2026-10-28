// Real test hardware: the rig board (ESP32-S3 + 5 INA219 + 5 relays) is an
// I2C slave on the CrowPanel's I2C header, shared with the touch controller.
// The link uses LovyanGFX's I2C functions on the touch controller's port so
// both share one bus driver (an Arduino Wire on the same pins would break touch).
#include <Arduino.h>
#include "config.h"
#include "hw.h"

#if RIG_LINK
#include <LovyanGFX.hpp>

static const int I2C_PORT = 1;  // same port as the GT911 touch (display.cpp)
static const int PKT_LEN = 6 + NUM_CH * 8 + 1;

struct LinkCh {
  float live, peak, avg;
  uint8_t resets;     // rig board's reset counter
  uint8_t expected;   // resets we have asked for
  bool hasAvg;
  bool overRange;
};

static LinkCh s_ch[NUM_CH];
static uint8_t s_relayMask = 0;   // what we asked for
static uint8_t s_present = 0;
static float s_rangeA = CURRENT_RANGE_A;
static bool s_synced = false;      // counters aligned with the rig board
static uint32_t s_lastOk = 0, s_lastPoll = 0;
static uint32_t s_errors = 0;

static bool send_cmd(uint8_t resetMask) {
  uint8_t cmd[3] = {0x01, s_relayMask, resetMask};
  for (int attempt = 0; attempt < 3; attempt++) {
    if (lgfx::i2c::transactionWrite(I2C_PORT, RIG_I2C_ADDR, cmd, 3, RIG_I2C_HZ).has_value()) return true;
    delayMicroseconds(500);
  }
  s_errors++;
  return false;
}

static bool read_status() {
  uint8_t p[PKT_LEN];
  if (!lgfx::i2c::transactionRead(I2C_PORT, RIG_I2C_ADDR, p, PKT_LEN, RIG_I2C_HZ).has_value()) return false;
  uint8_t sum = 0;
  for (int i = 0; i < PKT_LEN - 1; i++) sum += p[i];
  if (p[0] != 0xA5 || p[1] != 1 || sum != p[PKT_LEN - 1]) return false;

  s_present = p[3];
  if (p[4]) s_rangeA = p[4] / 10.0f;
  for (int i = 0; i < NUM_CH; i++) {
    const uint8_t *c = p + 6 + i * 8;
    LinkCh &ch = s_ch[i];
    ch.live = (int16_t)(c[0] | (c[1] << 8)) / 1000.0f;
    ch.peak = (int16_t)(c[2] | (c[3] << 8)) / 1000.0f;
    ch.avg = (int16_t)(c[4] | (c[5] << 8)) / 1000.0f;
    ch.resets = c[6];
    ch.overRange = c[7] & 1;
    ch.hasAvg = c[7] & 2;
    if (!s_synced) ch.expected = ch.resets;
    if (ch.live < 0) ch.live = 0;
  }
  s_synced = true;
  return true;
}

void hw_begin() {
  // The I2C port is set up by the touch driver in display_init(); nothing to do.
}

void hw_poll(uint32_t now) {
  if (now - s_lastPoll < RIG_POLL_MS) return;
  s_lastPoll = now;
  if (read_status()) {
    s_lastOk = now;
  } else {
    s_errors++;
    if (now - s_lastOk > 1000) s_synced = false;  // re-align counters after a restart
  }
}

bool hw_ok() { return s_synced && millis() - s_lastOk < 1000; }
bool hw_channel_ok(int ch) { return hw_ok() && ((s_present >> ch) & 1); }
bool hw_has_contacts() { return false; }  // continuity inputs not fitted yet
float hw_range_a() { return s_rangeA; }

void hw_sim_set_spec(const ModelSpec *) {}
void hw_new_cycle(int) {}

void hw_relay(int ch, bool on, uint32_t) {
  uint8_t bit = 1 << ch;
  uint8_t before = s_relayMask;
  s_relayMask = on ? (s_relayMask | bit) : (s_relayMask & ~bit);
  uint8_t reset = (on && !(before & bit)) ? bit : 0;  // new ON: restart peak / average
  if (reset) s_ch[ch].expected++;
  if (s_relayMask != before || reset) send_cmd(reset);
}

void hw_all_off(uint32_t) {
  s_relayMask = 0;
  send_cmd(0);
}

bool hw_relay_state(int ch) { return (s_relayMask >> ch) & 1; }

float hw_current(int ch, uint32_t) { return s_ch[ch].live; }

bool hw_contact_closed(int, uint32_t) { return false; }

void hw_stats_reset(int ch) {
  s_ch[ch].expected++;
  send_cmd(1 << ch);
}

// Only valid once the rig board has confirmed our last reset
static bool fresh(int ch) { return hw_ok() && s_ch[ch].resets == s_ch[ch].expected; }

float hw_peak(int ch) {
  if (!fresh(ch)) return -1;
  return s_ch[ch].overRange ? s_rangeA : s_ch[ch].peak;
}

float hw_avg(int ch) {
  if (!fresh(ch) || !s_ch[ch].hasAvg) return -1;
  return s_ch[ch].avg;
}

#endif  // RIG_LINK
