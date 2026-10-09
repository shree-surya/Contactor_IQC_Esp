#include <Arduino.h>
#include <stdarg.h>
#include <esp_system.h>
#include "rig.h"
#include "hw_sim.h"

// Per-channel test sequence (one cycle):
//   1. Open      relay OFF: contact must be open and coil current ~0
//   2. In-rush   relay ON, peak current over the in-rush window
//   3. Cont      average current from settleMs to settleMs + avgMs
//   4. Contin.   contact must stay closed for the whole averaging window
//   5. Release   relay OFF, after releaseMs contact open and current ~0
// Channels start staggerMs apart and never overlap their in-rush windows.
// A channel stops at its first failure; the others carry on.

enum Phase : uint8_t { P_IDLE, P_WAIT_START, P_INRUSH, P_SETTLE, P_AVG, P_RELEASE, P_GAP, P_DONE };

struct ChRun {
  Phase ph;
  uint32_t startAt, relayOnAt, t0;
  float peak;
  double sum;
  uint32_t n;
  bool continDrop;
};

static ChRun s_run[NUM_CH];
static ChannelView s_view[NUM_CH];
static const ModelSpec *s_spec = nullptr;

static ResultRow s_queue[RESULT_QUEUE_LEN];
static int s_qHead = 0, s_qCount = 0;

static char s_event[96] = "Ready";

static void set_event(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_event, sizeof(s_event), fmt, ap);
  va_end(ap);
  Serial.printf("[rig] %s\n", s_event);
}

static const char *pf_txt(StepState s) {
  switch (s) {
    case ST_PASS: return "PASS";
    case ST_FAIL: return "FAIL";
    default: return "-NA-";
  }
}

static CycleResult s_cyc[NUM_CH][MAX_CYCLES];

static void fill_na(CycleResult &c) {
  strlcpy(c.open, "-NA-", sizeof(c.open));
  strlcpy(c.inrushA, "-NA-", sizeof(c.inrushA));
  strlcpy(c.inrushPF, "-NA-", sizeof(c.inrushPF));
  strlcpy(c.contA, "-NA-", sizeof(c.contA));
  strlcpy(c.contPF, "-NA-", sizeof(c.contPF));
  strlcpy(c.contin, "-NA-", sizeof(c.contin));
  strlcpy(c.release, "-NA-", sizeof(c.release));
}

// Stores the current cycle of a channel (called when the cycle ends)
static void record_cycle(int ch) {
  const ChannelView &v = s_view[ch];
  if (v.cycle == 0 || v.cycle > MAX_CYCLES) return;
  CycleResult &c = s_cyc[ch][v.cycle - 1];
  strlcpy(c.open, pf_txt(v.open), sizeof(c.open));
  if (v.inrushMeasured) snprintf(c.inrushA, sizeof(c.inrushA), "%.2f", v.inrushA);
  else strlcpy(c.inrushA, "-NA-", sizeof(c.inrushA));
  if (v.inrush == ST_NA && v.inrushMeasured) strlcpy(c.inrushPF, "NA", sizeof(c.inrushPF));
  else strlcpy(c.inrushPF, pf_txt(v.inrush), sizeof(c.inrushPF));
  if (v.contMeasured) snprintf(c.contA, sizeof(c.contA), "%.3f", v.contA);
  else strlcpy(c.contA, "-NA-", sizeof(c.contA));
  strlcpy(c.contPF, pf_txt(v.cont), sizeof(c.contPF));
  strlcpy(c.contin, pf_txt(v.contin), sizeof(c.contin));
  strlcpy(c.release, pf_txt(v.release), sizeof(c.release));
}

// Queues the finished unit as one Results row; cycles not run are "-NA-"
static void push_unit(int ch, const char *overall, const char *failStep) {
  const ChannelView &v = s_view[ch];
  ResultRow r;
  memset(&r, 0, sizeof(r));
  strlcpy(r.serial, g_app.serial[ch], sizeof(r.serial));
  strlcpy(r.op, g_app.operatorName, sizeof(r.op));
  strlcpy(r.model, s_spec ? s_spec->name : "", sizeof(r.model));
  r.ch = ch + 1;
  r.cycles = s_spec ? s_spec->cycles : 0;
  if (r.cycles > MAX_CYCLES) r.cycles = MAX_CYCLES;
  strlcpy(r.overall, overall, sizeof(r.overall));
  strlcpy(r.failStep, failStep, sizeof(r.failStep));
  for (int i = 0; i < r.cycles; i++) {
    if (i < v.cycle) r.cyc[i] = s_cyc[ch][i];
    else fill_na(r.cyc[i]);
  }

  int idx = (s_qHead + s_qCount) % RESULT_QUEUE_LEN;
  if (s_qCount == RESULT_QUEUE_LEN) {
    s_qHead = (s_qHead + 1) % RESULT_QUEUE_LEN;  // full: drop oldest
    Serial.println("[rig] result queue full, oldest row dropped");
  } else {
    s_qCount++;
  }
  s_queue[idx] = r;
}

// Marks still-pending steps as not tested
static void mark_rest_na(ChannelView &v) {
  StepState *steps[] = {&v.open, &v.inrush, &v.cont, &v.contin, &v.release};
  for (StepState *s : steps) {
    if (*s == ST_NONE || *s == ST_RUN) *s = ST_NA;
  }
}

static void fail(int ch, StepState *step, const char *name, uint32_t now) {
  ChannelView &v = s_view[ch];
  hw_relay(ch, false, now);
  *step = ST_FAIL;
  mark_rest_na(v);
  strlcpy(v.failStep, name, sizeof(v.failStep));
  v.status = CH_FAIL;
  s_run[ch].ph = P_DONE;
  record_cycle(ch);
  push_unit(ch, "FAIL", name);
  set_event("CH%d cycle %d FAIL: %s", ch + 1, v.cycle, name);
}

static bool inrush_busy(int self) {
  for (int i = 0; i < NUM_CH; i++) {
    if (i != self && s_run[i].ph == P_INRUSH) return true;
  }
  return false;
}

static void begin_cycle(int ch, uint32_t now) {
  ChRun &r = s_run[ch];
  ChannelView &v = s_view[ch];
  hw_new_cycle(ch);
  v.cycle++;
  v.status = CH_RUNNING;
  v.open = v.inrush = v.cont = v.contin = v.release = ST_NONE;
  v.inrushA = v.contA = 0;
  v.inrushMeasured = v.contMeasured = false;
  v.failStep[0] = '\0';

  bool closed = hw_contact_closed(ch, now);
  float cur = hw_current(ch, now);
  if (closed || cur >= OFF_CURRENT_MAX_A) {
    fail(ch, &v.open, closed ? "Open (contact closed)" : "Open (current)", now);
    return;
  }
  v.open = ST_PASS;

  hw_relay(ch, true, now);
  r.relayOnAt = now;
  r.peak = 0;
  r.ph = P_INRUSH;
  v.inrush = ST_RUN;
}

static void tick_channel(int ch, uint32_t now) {
  ChRun &r = s_run[ch];
  ChannelView &v = s_view[ch];
  const ModelSpec &m = *s_spec;
  float cur = hw_current(ch, now);
  v.liveA = cur;

  switch (r.ph) {
    case P_WAIT_START:
      if ((int32_t)(now - r.startAt) >= 0 && !inrush_busy(ch)) begin_cycle(ch, now);
      break;

    case P_INRUSH:
      if (cur > r.peak) r.peak = cur;
      v.inrushA = r.peak;
      if (now - r.relayOnAt >= m.inrushWinMs) {
        v.inrushMeasured = true;
        if (r.peak >= CURRENT_RANGE_A - 0.01f) {
          fail(ch, &v.inrush, "In-rush OVER RANGE", now);
          break;
        }
        if (m.hasInrush()) {
          if (r.peak < m.inrushLsl || r.peak > m.inrushUsl) {
            fail(ch, &v.inrush, "In-rush", now);
            break;
          }
          v.inrush = ST_PASS;
        } else {
          v.inrush = ST_NA;  // measured, no limit for this model
        }
        v.cont = ST_RUN;
        v.contin = ST_RUN;
        r.ph = P_SETTLE;
      }
      break;

    case P_SETTLE:
      if (now - r.relayOnAt >= m.settleMs) {
        r.sum = 0;
        r.n = 0;
        r.continDrop = false;
        r.ph = P_AVG;
      }
      break;

    case P_AVG:
      r.sum += cur;
      r.n++;
      v.contA = (float)(r.sum / r.n);
      if (!hw_contact_closed(ch, now)) r.continDrop = true;
      if (now - r.relayOnAt >= (uint32_t)m.settleMs + m.avgMs) {
        hw_relay(ch, false, now);
        v.contMeasured = true;
        if (v.contA < m.contLsl || v.contA > m.contUsl) {
          fail(ch, &v.cont, "Continuous current", now);
          break;
        }
        v.cont = ST_PASS;
        if (r.continDrop) {
          fail(ch, &v.contin, "Continuity", now);
          break;
        }
        v.contin = ST_PASS;
        v.release = ST_RUN;
        r.t0 = now;
        r.ph = P_RELEASE;
      }
      break;

    case P_RELEASE:
      if (now - r.t0 >= m.releaseMs) {
        if (hw_contact_closed(ch, now) || cur >= OFF_CURRENT_MAX_A) {
          fail(ch, &v.release, "Release", now);
          break;
        }
        v.release = ST_PASS;
        record_cycle(ch);
        if (v.cycle >= m.cycles) {
          push_unit(ch, "PASS", "");
          v.status = CH_PASS;
          r.ph = P_DONE;
          set_event("CH%d PASS - all %d cycles", ch + 1, m.cycles);
        } else {
          set_event("CH%d cycle %d/%d PASS", ch + 1, v.cycle, m.cycles);
          r.t0 = now;
          r.ph = P_GAP;
          v.status = CH_WAITING;  // resting until the next cycle starts
        }
      }
      break;

    case P_GAP:
      if (now - r.t0 >= m.cycleGapMs) {
        r.startAt = now;
        r.ph = P_WAIT_START;
        v.status = CH_WAITING;
      }
      break;

    default:
      break;
  }
}

void rig_begin() {
  randomSeed(esp_random());
  bool none[NUM_CH] = {false};
  rig_reset(none);
}

void rig_tick() {
  if (!s_spec) return;
  uint32_t now = millis();
  for (int ch = 0; ch < NUM_CH; ch++) tick_channel(ch, now);
}

void rig_reset(const bool enabled[NUM_CH]) {
  hw_all_off(millis());
  for (int ch = 0; ch < NUM_CH; ch++) {
    memset(&s_run[ch], 0, sizeof(ChRun));
    memset(&s_view[ch], 0, sizeof(ChannelView));
    s_run[ch].ph = P_IDLE;
    s_view[ch].status = enabled[ch] ? CH_IDLE : CH_SKIPPED;
  }
  set_event("Ready");
}

void rig_start(const ModelSpec *spec, const bool enabled[NUM_CH]) {
  if (rig_running()) return;
  rig_reset(enabled);
  rig_clear_results();
  memset(s_cyc, 0, sizeof(s_cyc));
  s_spec = spec;
  hw_sim_set_spec(spec);

  uint32_t now = millis();
  int k = 0;
  for (int ch = 0; ch < NUM_CH; ch++) {
    if (!enabled[ch]) continue;
    s_run[ch].ph = P_WAIT_START;
    s_run[ch].startAt = now + (uint32_t)k * spec->staggerMs;
    s_view[ch].status = CH_WAITING;
    k++;
  }
  set_event("Batch started: %d channel(s)", k);
}

void rig_stop_all() {
  uint32_t now = millis();
  for (int ch = 0; ch < NUM_CH; ch++) {
    ChRun &r = s_run[ch];
    ChannelView &v = s_view[ch];
    if (r.ph == P_IDLE || r.ph == P_DONE) continue;
    bool midCycle = r.ph == P_INRUSH || r.ph == P_SETTLE || r.ph == P_AVG || r.ph == P_RELEASE;
    hw_relay(ch, false, now);
    if (midCycle) {
      mark_rest_na(v);
      record_cycle(ch);
    }
    strlcpy(v.failStep, "ABORTED", sizeof(v.failStep));
    push_unit(ch, "ABORTED", "ABORTED");
    v.status = CH_ABORTED;
    r.ph = P_DONE;
  }
  hw_all_off(now);
  set_event("STOPPED by operator - all relays OFF");
}

bool rig_running() {
  for (int ch = 0; ch < NUM_CH; ch++) {
    if (s_run[ch].ph != P_IDLE && s_run[ch].ph != P_DONE) return true;
  }
  return false;
}

bool rig_is_simulated() { return true; }

const ChannelView &rig_channel(int ch) { return s_view[ch]; }

int rig_round() {
  int r = 0;
  for (int ch = 0; ch < NUM_CH; ch++) {
    if (s_view[ch].cycle > r) r = s_view[ch].cycle;
  }
  return r;
}

const char *rig_last_event() { return s_event; }

int rig_pending_count() { return s_qCount; }

void rig_clear_results() {
  s_qHead = 0;
  s_qCount = 0;
}

bool rig_pop_result(ResultRow *out) {
  if (s_qCount == 0) return false;
  *out = s_queue[s_qHead];
  s_qHead = (s_qHead + 1) % RESULT_QUEUE_LEN;
  s_qCount--;
  return true;
}

bool rig_manual_relay(int ch, bool on) {
  if (rig_running()) return false;
  if (on) {
    if (!s_spec && g_modelCount > 0) hw_sim_set_spec(&g_models[0]);
    hw_new_cycle(ch);
  }
  hw_relay(ch, on, millis());
  return true;
}

bool rig_relay_state(int ch) { return hw_relay_state(ch); }

bool rig_contact_closed(int ch) { return hw_contact_closed(ch, millis()); }

float rig_current(int ch) { return hw_current(ch, millis()); }
