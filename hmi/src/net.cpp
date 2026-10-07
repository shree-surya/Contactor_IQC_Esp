#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "net.h"

// Optional factory WiFi from include/secrets.h (git-ignored). Used until other
// credentials are saved on the Admin screen.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

static const char *PREF_NS = "iqc_net";
static char s_ssid[40] = "";  // SSID in use (saved one, or the factory default)

void net_begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  Preferences prefs;
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString("ssid", WIFI_SSID);
  String pass = prefs.getString("pass", WIFI_PASS);
  prefs.end();

  strlcpy(s_ssid, ssid.c_str(), sizeof(s_ssid));
  if (ssid.length()) WiFi.begin(ssid.c_str(), pass.c_str());
}

void net_save_and_connect(const char *ssid, const char *pass) {
  Preferences prefs;
  prefs.begin(PREF_NS, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  strlcpy(s_ssid, ssid, sizeof(s_ssid));
  WiFi.disconnect();
  WiFi.begin(ssid, pass);
}

bool net_connected() { return WiFi.status() == WL_CONNECTED; }

NetState net_state() {
  if (net_connected()) return NET_CONNECTED;
  return s_ssid[0] ? NET_CONNECTING : NET_NO_CONFIG;
}

int net_rssi() { return net_connected() ? WiFi.RSSI() : 0; }

void net_saved_ssid(char *buf, size_t len) {
  Preferences prefs;
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString("ssid", WIFI_SSID);
  prefs.end();
  strlcpy(buf, ssid.c_str(), len);
}

void net_ip(char *buf, size_t len) {
  if (net_connected()) {
    strlcpy(buf, WiFi.localIP().toString().c_str(), len);
  } else {
    strlcpy(buf, "-", len);
  }
}
