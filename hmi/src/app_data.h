#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"

// Limits and timings for one contactor model (one row of the "Specs" sheet).
struct ModelSpec {
  char name[NAME_MAX_LEN + 1];
  float inrushLsl, inrushUsl;  // NAN = in-rush not checked ("NA" in the sheet)
  float contLsl, contUsl;
  uint16_t inrushWinMs;        // peak search window after relay ON
  uint16_t settleMs;           // continuous-current window starts here (from relay ON)
  uint16_t avgMs;              // averaging + continuity window length
  uint16_t releaseMs;          // wait after relay OFF before the release check
  uint8_t cycles;
  uint16_t cycleGapMs;         // rest between cycles of the same channel
  uint16_t staggerMs;          // start offset between channels

  bool hasInrush() const { return !isnan(inrushLsl) && !isnan(inrushUsl); }
};

// What the operator has selected on the screens.
struct AppState {
  char operatorName[NAME_MAX_LEN + 1];
  int modelIdx;  // -1 = none selected
  bool chEnabled[NUM_CH];
  char serial[NUM_CH][SERIAL_MAX_LEN + 1];
};

extern ModelSpec g_models[MAX_MODELS];
extern int g_modelCount;
extern char g_operators[MAX_OPERATORS][NAME_MAX_LEN + 1];
extern int g_operatorCount;
extern AppState g_app;

// Loads built-in defaults (used until the Google Sheet has been read once).
void app_data_init();

// Fills timings with the defaults; limits are left untouched.
void app_data_default_timings(ModelSpec &m);

// Replaces models / operators (from the Sheet or its flash cache).
// The selected model is cleared because its index may have changed.
void app_data_set_models(const ModelSpec *models, int count);
void app_data_set_operators(const char names[][NAME_MAX_LEN + 1], int count);
