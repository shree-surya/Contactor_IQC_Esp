#pragma once
// ---------------------------------------------------------------------------
// Google Sheets credentials.
//
// 1. Copy this file to "secrets.h" in the same folder.
// 2. Fill in the values (see docs/GOOGLE_SHEETS_SETUP.md, Part D).
// secrets.h is git-ignored: never commit it or share it.
// ---------------------------------------------------------------------------

// Factory WiFi (used until other credentials are saved on the Admin screen)
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// From the Sheet URL: https://docs.google.com/spreadsheets/d/<GSHEET_ID>/edit
#define GSHEET_ID "paste-the-sheet-id-here"

// From the service-account JSON key file
#define GSA_PROJECT_ID "contactor-iqc-xxxxxx"
#define GSA_CLIENT_EMAIL "iqc-rig@contactor-iqc-xxxxxx.iam.gserviceaccount.com"

// Copy "private_key" from the JSON exactly, keeping every \n
#define GSA_PRIVATE_KEY "-----BEGIN PRIVATE KEY-----\nMIIE...\n-----END PRIVATE KEY-----\n"
