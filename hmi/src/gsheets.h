#pragma once
#include <stddef.h>
#include "rig.h"

// ---------------------------------------------------------------------------
// Google Sheets link (service account, Sheets API v4).
//
// - Reads "Specs" and "Operators" at boot and on SYNC NOW, caches them in
//   flash (LittleFS) so the rig works offline with the last known data.
// - Appends one "Results" row per channel per cycle. Rows are queued in flash
//   first and uploaded by a background task, so nothing is lost while WiFi
//   is down and the UI never waits on the network.
//
// Credentials come from include/secrets.h (git-ignored). Without that file
// the link is disabled and the rig keeps working with built-in defaults.
// ---------------------------------------------------------------------------

void gsheets_begin();  // mount flash, load cached specs/operators, start background task

void gsheets_enqueue(const ResultRow &row);
void gsheets_request_sync();  // re-read Specs + Operators

// Freshly synced specs/operators are held back until the UI says it is safe
// to swap them in (never in the middle of a test).
bool gsheets_has_new_data();
void gsheets_apply_new_data();

int gsheets_pending_count();
bool gsheets_configured();
void gsheets_status(char *buf, size_t len);  // e.g. "OK", "Waiting for WiFi", "Error: ..."
void gsheets_last_sync(char *buf, size_t len);
bool gsheets_time_valid();
void gsheets_now_str(char *buf, size_t len);  // "YYYY-MM-DD HH:MM:SS" or "NO TIME"
