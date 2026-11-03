#pragma once
#include <stddef.h>
#include "rig.h"

// ---------------------------------------------------------------------------
// Google Sheets link (service account, Sheets API v4).
//
// - Reads "Specs" and "Operators" at boot and on SYNC NOW, caches them in
//   flash (LittleFS) so the rig works offline with the last known data.
// - Appends one "Results" row per unit when the operator presses SAVE. Rows
//   are uploaded from RAM by a background task; only if that fails (no WiFi,
//   error) are they kept in flash and retried, so nothing is lost and the
//   screen isn't disturbed by flash writes in the normal case.
//
// Credentials come from include/secrets.h (git-ignored). Without that file
// the link is disabled and the rig keeps working with built-in defaults.
// ---------------------------------------------------------------------------

void gsheets_begin();  // mount flash, load cached specs/operators, start background task

void gsheets_enqueue(const ResultRow &row);
void gsheets_request_sync();  // re-read Specs + Operators
void gsheets_flush_now();     // upload queued rows now (skip the retry wait)
void gsheets_set_hold(bool hold);  // true while a test runs: no Sheet traffic

enum GsState { GS_OFF, GS_NO_WIFI, GS_SYNCING, GS_SYNCED, GS_ERROR };
GsState gsheets_state();      // Specs/Operators sync state, for the login screen
bool gsheets_has_cache();     // a synced copy of Specs/Operators is in flash

// Freshly synced specs/operators are held back until the UI says it is safe
// to swap them in (never in the middle of a test).
bool gsheets_has_new_data();
void gsheets_apply_new_data();

int gsheets_pending_count();

struct PendingRow {
  char serial[SERIAL_MAX_LEN + 1];
  char ts[24];
  char op[NAME_MAX_LEN + 1];
  char model[NAME_MAX_LEN + 1];
  uint8_t ch;
  char overall[10];
  char failStep[24];
};
// Rows saved on the panel but not in the Sheet yet, oldest first, after skipping the first `skip`
int gsheets_pending_rows(PendingRow *out, int max, int skip);
bool gsheets_configured();
void gsheets_status(char *buf, size_t len);  // e.g. "OK", "Waiting for WiFi", "Error: ..."
void gsheets_last_sync(char *buf, size_t len);
bool gsheets_time_valid();
void gsheets_now_str(char *buf, size_t len);  // "YYYY-MM-DD HH:MM:SS" or "NO TIME"
