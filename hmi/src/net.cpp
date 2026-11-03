#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "net.h"

// Optional factory WiFi from include/secrets.h (git-ignored). Saved as the
// first network on the very first boot.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

static const char *PREF_NS = "iqc_net";
static const uint32_t ATTEMPT_MS = 12000;     // give one connection attempt this long
static const uint32_t SCAN_FRESH_MS = 60000;  // scan results used to order the attempts

struct Saved {
  char ssid[33];
  char pass[65];
};

static Saved s_saved[NET_MAX_SAVED];
static int s_count = 0;

static int s_trying = -1;         // saved index being tried, -1 = none
static int s_want = -1;           // network the operator just chose: try it first
static uint32_t s_attemptAt = 0;  // start of the current / last attempt
static bool s_attempted = false;
static int s_rr = 0;              // round-robin position over the saved list
static char s_curSsid[33] = "";   // network we are connected to
static char s_lastSsid[33] = "";  // last network tried
static volatile uint8_t s_reason = 0;  // last disconnect reason (WiFi event task)
static bool s_failed = false;     // last attempt failed (for the status text)
static uint8_t s_failReason = 0;

static NetScanItem s_scan[NET_MAX_SCAN];
static int s_scanN = 0;
static uint32_t s_scanAt = 0;
static bool s_scanning = false;

// ---------------------------------------------------------------------------
// Saved list in NVS: n, s0..s4, p0..p4
// ---------------------------------------------------------------------------

static void save_list() {
  Preferences prefs;
  prefs.begin(PREF_NS, false);
  prefs.putUChar("n", (uint8_t)s_count);
  for (int i = 0; i < NET_MAX_SAVED; i++) {
    char ks[4], kp[4];
    snprintf(ks, sizeof(ks), "s%d", i);
    snprintf(kp, sizeof(kp), "p%d", i);
    if (i < s_count) {
      prefs.putString(ks, s_saved[i].ssid);
      prefs.putString(kp, s_saved[i].pass);
    } else {
      prefs.remove(ks);
      prefs.remove(kp);
    }
  }
  prefs.end();
}

static void load_list() {
  Preferences prefs;
  prefs.begin(PREF_NS, true);
  bool hasList = prefs.isKey("n");
  s_count = 0;
  if (hasList) {
    int n = prefs.getUChar("n", 0);
    for (int i = 0; i < n && i < NET_MAX_SAVED; i++) {
      char ks[4], kp[4];
      snprintf(ks, sizeof(ks), "s%d", i);
      snprintf(kp, sizeof(kp), "p%d", i);
      String s = prefs.getString(ks, ""), p = prefs.getString(kp, "");
      if (!s.length()) continue;
      strlcpy(s_saved[s_count].ssid, s.c_str(), sizeof(s_saved[0].ssid));
      strlcpy(s_saved[s_count].pass, p.c_str(), sizeof(s_saved[0].pass));
      s_count++;
    }
  }
  // Older firmware kept one network as "ssid"/"pass"; else the factory default
  String oldSsid = hasList ? String("") : prefs.getString("ssid", WIFI_SSID);
  String oldPass = hasList ? String("") : prefs.getString("pass", WIFI_PASS);
  prefs.end();
  if (!hasList) {
    if (oldSsid.length()) {
      strlcpy(s_saved[0].ssid, oldSsid.c_str(), sizeof(s_saved[0].ssid));
      strlcpy(s_saved[0].pass, oldPass.c_str(), sizeof(s_saved[0].pass));
      s_count = 1;
    }
    save_list();
  }
}

static int find_saved(const char *ssid) {
  for (int i = 0; i < s_count; i++) {
    if (strcmp(s_saved[i].ssid, ssid) == 0) return i;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Connection manager
// ---------------------------------------------------------------------------

static void on_event(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_reason = info.wifi_sta_disconnected.reason;
}

static int scan_rssi(const char *ssid) {
  for (int i = 0; i < s_scanN; i++) {
    if (strcmp(s_scan[i].ssid, ssid) == 0) return s_scan[i].rssi;
  }
  return -1000;
}

// Next saved network to try: the operator's choice first, then round robin,
// strongest first when a recent scan is known
static int next_candidate(uint32_t now) {
  if (s_want >= 0 && s_want < s_count) {
    int w = s_want;
    s_want = -1;
    return w;
  }
  if (s_count == 0) return -1;
  int order[NET_MAX_SAVED];
  for (int i = 0; i < s_count; i++) order[i] = i;
  if (s_scanN > 0 && now - s_scanAt < SCAN_FRESH_MS) {
    for (int i = 1; i < s_count; i++) {
      for (int j = i; j > 0 && scan_rssi(s_saved[order[j]].ssid) > scan_rssi(s_saved[order[j - 1]].ssid); j--) {
        int t = order[j];
        order[j] = order[j - 1];
        order[j - 1] = t;
      }
    }
  }
  return order[(s_rr++) % s_count];
}

static void start_attempt(int idx, uint32_t now) {
  s_trying = idx;
  s_attemptAt = now;
  s_attempted = true;
  s_reason = 0;
  strlcpy(s_lastSsid, s_saved[idx].ssid, sizeof(s_lastSsid));
  Serial.printf("[wifi] connecting to %s\n", s_lastSsid);
  WiFi.disconnect(false, false);
  WiFi.begin(s_saved[idx].ssid, s_saved[idx].pass);
}

void net_begin() {
  WiFi.persistent(false);  // the saved list lives in our own NVS keys
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);    // modem power save causes random drop-outs
  WiFi.setAutoReconnect(false);  // net_tick() does the reconnecting
  WiFi.onEvent(on_event);
  load_list();
  if (s_count) start_attempt(next_candidate(millis()), millis());
}

void net_tick(uint32_t now) {
  if (s_scanning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    s_scanN = 0;
    for (int i = 0; i < n && s_scanN < NET_MAX_SCAN; i++) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;  // hidden network
      int dup = -1;
      for (int k = 0; k < s_scanN; k++) {
        if (strcmp(s_scan[k].ssid, ssid.c_str()) == 0) dup = k;
      }
      int rssi = WiFi.RSSI(i);
      if (dup >= 0) {  // same name from several access points: keep the strongest
        if (rssi > s_scan[dup].rssi) s_scan[dup].rssi = rssi;
        continue;
      }
      NetScanItem &it = s_scan[s_scanN++];
      strlcpy(it.ssid, ssid.c_str(), sizeof(it.ssid));
      it.rssi = rssi;
      it.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    for (int i = 1; i < s_scanN; i++) {  // strongest first
      for (int j = i; j > 0 && s_scan[j].rssi > s_scan[j - 1].rssi; j--) {
        NetScanItem t = s_scan[j];
        s_scan[j] = s_scan[j - 1];
        s_scan[j - 1] = t;
      }
    }
    s_scanAt = now;
    s_scanning = false;
    if (!net_connected()) s_attemptAt = now - NET_RETRY_MS;  // retry straight away
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (s_trying >= 0 || !s_curSsid[0]) {
      strlcpy(s_curSsid, WiFi.SSID().c_str(), sizeof(s_curSsid));
      Serial.printf("[wifi] connected to %s, IP %s\n", s_curSsid, WiFi.localIP().toString().c_str());
      s_trying = -1;
      s_failed = false;
    }
    return;
  }
  if (s_curSsid[0]) {
    Serial.printf("[wifi] lost %s (reason %u)\n", s_curSsid, s_reason);
    s_curSsid[0] = '\0';
    s_attemptAt = now - NET_RETRY_MS;  // reconnect straight away
  }
  if (s_trying >= 0 && now - s_attemptAt < ATTEMPT_MS) return;  // attempt still running
  if (s_trying >= 0) {
    s_failed = true;
    s_failReason = s_reason;
    Serial.printf("[wifi] %s failed (reason %u)\n", s_lastSsid, s_failReason);
    WiFi.disconnect(false, false);
    s_trying = -1;
  }
  bool due = !s_attempted || s_want >= 0 || now - s_attemptAt >= NET_RETRY_MS;
  if (!due) return;
  int idx = next_candidate(now);
  if (idx >= 0) start_attempt(idx, now);
}

bool net_connected() { return WiFi.status() == WL_CONNECTED; }

NetState net_state() {
  if (net_connected()) return NET_CONNECTED;
  return s_count ? NET_CONNECTING : NET_NO_CONFIG;
}

int net_rssi() { return net_connected() ? WiFi.RSSI() : 0; }

void net_ip(char *buf, size_t len) {
  if (net_connected()) strlcpy(buf, WiFi.localIP().toString().c_str(), len);
  else strlcpy(buf, "-", len);
}

void net_saved_ssid(char *buf, size_t len) {
  if (s_curSsid[0]) strlcpy(buf, s_curSsid, len);
  else strlcpy(buf, s_count ? s_saved[0].ssid : "", len);
}

void net_status_text(char *buf, size_t len) {
  if (net_connected()) {
    snprintf(buf, len, "Connected to %s", s_curSsid);
  } else if (s_count == 0) {
    strlcpy(buf, "No WiFi saved", len);
  } else if (s_scanning) {
    strlcpy(buf, "Scanning...", len);
  } else if (s_trying >= 0) {
    snprintf(buf, len, "Connecting to %s...", s_lastSsid);
  } else if (s_failed) {
    const char *why = "Could not connect to";
    switch (s_failReason) {
      case WIFI_REASON_AUTH_FAIL:
      case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      case WIFI_REASON_HANDSHAKE_TIMEOUT:
      case WIFI_REASON_AUTH_EXPIRE:
        why = "Wrong password for";
        break;
      case WIFI_REASON_NO_AP_FOUND:
        why = "Not in range:";
        break;
    }
    snprintf(buf, len, "%s %s - retrying", why, s_lastSsid);
  } else {
    strlcpy(buf, "Waiting to connect...", len);
  }
}

int net_saved_count() { return s_count; }
const char *net_saved_at(int i) { return (i >= 0 && i < s_count) ? s_saved[i].ssid : ""; }
bool net_is_saved(const char *ssid) { return find_saved(ssid) >= 0; }
bool net_is_current(const char *ssid) { return net_connected() && strcmp(s_curSsid, ssid) == 0; }

bool net_connect(const char *ssid, const char *pass) {
  int i = find_saved(ssid);
  if (i < 0) {
    if (s_count >= NET_MAX_SAVED) return false;
    i = s_count++;
    strlcpy(s_saved[i].ssid, ssid, sizeof(s_saved[i].ssid));
    s_saved[i].pass[0] = '\0';
  }
  if (pass && pass[0]) strlcpy(s_saved[i].pass, pass, sizeof(s_saved[i].pass));
  save_list();
  if (net_is_current(ssid)) return true;
  WiFi.disconnect(false, false);
  s_curSsid[0] = '\0';
  s_trying = -1;
  s_failed = false;
  s_want = i;  // net_tick() starts it on the next call
  return true;
}

void net_forget(const char *ssid) {
  int i = find_saved(ssid);
  if (i < 0) return;
  for (int k = i; k < s_count - 1; k++) s_saved[k] = s_saved[k + 1];
  s_count--;
  save_list();
  s_want = -1;
  if (strcmp(s_curSsid, ssid) == 0 || (s_trying >= 0 && strcmp(s_lastSsid, ssid) == 0)) {
    WiFi.disconnect(false, false);
    s_curSsid[0] = '\0';
    s_trying = -1;
    s_attemptAt = millis() - NET_RETRY_MS;  // move on to another saved network
  }
}

void net_scan_start() {
  if (s_scanning) return;
  if (!net_connected() && s_trying >= 0) {  // a scan can't run during a connection attempt
    WiFi.disconnect(false, false);
    s_trying = -1;
  }
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) return;
  s_scanning = true;
}

bool net_scan_busy() { return s_scanning; }

int net_scan_results(const NetScanItem **items) {
  *items = s_scan;
  return s_scanN;
}
