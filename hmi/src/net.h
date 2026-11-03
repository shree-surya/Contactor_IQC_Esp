#pragma once
#include <stddef.h>
#include <stdint.h>

// WiFi station handling. Up to NET_MAX_SAVED networks are stored in flash
// (NVS). net_tick() keeps the panel connected: while WiFi is down it tries
// the saved networks one after the other (strongest first when a recent scan
// is known) and keeps retrying every NET_RETRY_MS, forever.
#define NET_MAX_SAVED 5
#define NET_RETRY_MS 15000
#define NET_MAX_SCAN 16

void net_begin();
void net_tick(uint32_t now);  // call from loop()

bool net_connected();
enum NetState { NET_NO_CONFIG, NET_CONNECTING, NET_CONNECTED };
NetState net_state();
int net_rssi();
void net_ip(char *buf, size_t len);
void net_saved_ssid(char *buf, size_t len);  // network in use, else the first saved one
void net_status_text(char *buf, size_t len);  // e.g. "Connecting to QA_Lab...", "Wrong password for QA_Lab"

// Saved networks
int net_saved_count();
const char *net_saved_at(int i);
bool net_is_saved(const char *ssid);
bool net_is_current(const char *ssid);  // connected to this network now
// Saves (or updates) the network and connects to it now. An empty password
// keeps the saved one. False when NET_MAX_SAVED networks are already saved.
bool net_connect(const char *ssid, const char *pass);
void net_forget(const char *ssid);

// Scan for nearby networks (runs in the background, ~3 s)
struct NetScanItem {
  char ssid[33];
  int rssi;
  bool secure;
};
void net_scan_start();
bool net_scan_busy();
int net_scan_results(const NetScanItem **items);  // strongest first; 0 before the first scan
