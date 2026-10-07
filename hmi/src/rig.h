#pragma once
#include "app_data.h"

// ---------------------------------------------------------------------------
// Test rig interface used by the UI.
//
// For now the sequencer runs on this board against simulated hardware
// (hw_sim.cpp), so the screens can be developed and demonstrated before the
// sub-board exists. Later the same sequencer moves to the ESP32 sub-board and
// this interface is fed over the I2C link instead; the UI does not change.
// ---------------------------------------------------------------------------

enum StepState : uint8_t { ST_NONE, ST_RUN, ST_PASS, ST_FAIL, ST_NA };

enum ChStatus : uint8_t { CH_IDLE, CH_SKIPPED, CH_WAITING, CH_RUNNING, CH_PASS, CH_FAIL, CH_ABORTED };

struct ChannelView {
  ChStatus status;
  uint8_t cycle;  // current (or last) cycle, 1-based; 0 = not started
  StepState open, inrush, cont, contin, release;
  float inrushA;  // peak (live while the in-rush window is open)
  float contA;    // average (live while the averaging window is open)
  float liveA;    // instantaneous coil current
  bool inrushMeasured, contMeasured;
  char failStep[24];
};

// Results of one cycle. Untested values are "-NA-"; "NA" means the model has
// no limit for that parameter (measured but not judged).
struct CycleResult {
  char open[6];
  char inrushA[8], inrushPF[6];
  char contA[8], contPF[6];
  char contin[6];
  char release[6];
};

// One row of the "Results" sheet = one contactor (all its cycles side by side).
struct ResultRow {
  char serial[SERIAL_MAX_LEN + 1];
  char op[NAME_MAX_LEN + 1];
  char model[NAME_MAX_LEN + 1];
  uint8_t ch;
  uint8_t cycles;  // cycles configured for the model (columns written)
  char overall[10];  // PASS / FAIL / ABORTED
  char failStep[24];
  CycleResult cyc[MAX_CYCLES];
};

void rig_begin();
void rig_tick();  // call from loop()

void rig_reset(const bool enabled[NUM_CH]);  // clear views before a batch
void rig_start(const ModelSpec *spec, const bool enabled[NUM_CH]);
void rig_stop_all();
bool rig_running();
bool rig_is_simulated();

const ChannelView &rig_channel(int ch);
int rig_round();
const char *rig_last_event();

int rig_pending_count();
bool rig_pop_result(ResultRow *out);

// Admin / manual mode (only while no batch is running)
bool rig_manual_relay(int ch, bool on);
bool rig_relay_state(int ch);
bool rig_contact_closed(int ch);
float rig_current(int ch);
