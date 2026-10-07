#pragma once
#include <stddef.h>

// WiFi station handling. Credentials are entered on the Admin screen and
// stored in flash (NVS); the panel reconnects on boot and after drop-outs.
void net_begin();
void net_save_and_connect(const char *ssid, const char *pass);
bool net_connected();
int net_rssi();
void net_saved_ssid(char *buf, size_t len);
void net_ip(char *buf, size_t len);
