#include <Arduino.h>
#include "hw_sim.h"

enum Fault : uint8_t { F_NONE, F_INRUSH_HIGH, F_CONT_LOW, F_NO_CLOSE, F_WELD, F_CHATTER };

struct SimCh {
  bool relay;
  uint32_t onAt, offAt;
  float peak, hold;
  Fault fault;
  bool welded;
};

static SimCh s_ch[NUM_CH];
static const ModelSpec *s_spec = nullptr;

static float frand(float lo, float hi) { return lo + (hi - lo) * (random(10000) / 10000.0f); }

void hw_sim_set_spec(const ModelSpec *spec) { s_spec = spec; }

void hw_new_cycle(int ch) {
  SimCh &c = s_ch[ch];
  float nomPeak = 2.0f, nomHold = 0.3f;
  if (s_spec) {
    if (s_spec->hasInrush()) nomPeak = (s_spec->inrushLsl + s_spec->inrushUsl) / 2;
    nomHold = (s_spec->contLsl + s_spec->contUsl) / 2;
  }
  c.peak = nomPeak * frand(0.92f, 1.08f);
  c.hold = nomHold * frand(0.95f, 1.05f);
  c.welded = false;

  // About 2% of cycles get a fault
  int r = random(1000);
  if (r < 6) c.fault = F_INRUSH_HIGH;
  else if (r < 10) c.fault = F_CONT_LOW;
  else if (r < 13) c.fault = F_NO_CLOSE;
  else if (r < 16) c.fault = F_WELD;
  else if (r < 20) c.fault = F_CHATTER;
  else c.fault = F_NONE;

  if (s_spec && c.fault == F_INRUSH_HIGH) c.peak = (s_spec->hasInrush() ? s_spec->inrushUsl : 2.5f) * 1.15f;
  if (s_spec && c.fault == F_CONT_LOW) c.hold = s_spec->contLsl * 0.8f;
}

void hw_relay(int ch, bool on, uint32_t now) {
  SimCh &c = s_ch[ch];
  if (on == c.relay) return;
  c.relay = on;
  if (on) {
    c.onAt = now;
    if (c.fault == F_WELD) c.welded = true;
  } else {
    c.offAt = now;
  }
}

void hw_all_off(uint32_t now) {
  for (int i = 0; i < NUM_CH; i++) hw_relay(i, false, now);
}

bool hw_relay_state(int ch) { return s_ch[ch].relay; }

float hw_current(int ch, uint32_t now) {
  const SimCh &c = s_ch[ch];
  float i = 0;
  if (c.relay) {
    float dt = (float)(now - c.onAt);
    float peak = c.peak > 0 ? c.peak : 2.0f;
    float hold = c.hold > 0 ? c.hold : 0.3f;
    if (dt < 20) i = peak * dt / 20.0f;
    else if (dt < 120) i = peak;
    else i = hold + (peak - hold) * expf(-(dt - 120.0f) / 80.0f);
    i *= frand(0.985f, 1.015f);
  }
  i += frand(-0.002f, 0.002f);
  if (i < 0) i = 0;
  if (i > CURRENT_RANGE_A) i = CURRENT_RANGE_A;  // INA219 saturates
  return i;
}

bool hw_contact_closed(int ch, uint32_t now) {
  const SimCh &c = s_ch[ch];
  if (c.welded) return true;
  if (c.relay) {
    uint32_t dt = now - c.onAt;
    if (c.fault == F_NO_CLOSE) return false;
    if (c.fault == F_CHATTER && dt >= 3500 && dt < 3600) return false;
    return dt >= 40;  // mechanical closing time
  }
  return (now - c.offAt) < 15;  // opening time
}
