#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "net.h"

static const char *PREF_NS = "iqc_net";

void net_begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  Preferences prefs;
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.length()) WiFi.begin(ssid.c_str(), pass.c_str());
}

void net_save_and_connect(const char *ssid, const char *pass) {
  Preferences prefs;
  prefs.begin(PREF_NS, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  WiFi.disconnect();
  WiFi.begin(ssid, pass);
}

bool net_connected() { return WiFi.status() == WL_CONNECTED; }

int net_rssi() { return net_connected() ? WiFi.RSSI() : 0; }

void net_saved_ssid(char *buf, size_t len) {
  Preferences prefs;
  prefs.begin(PREF_NS, true);
  String ssid = prefs.getString("ssid", "");
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
