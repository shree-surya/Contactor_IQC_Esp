// Bench test: 5 relays + 5 INA219 together
// Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, cable in "USB Native"
//
// Wiring: sensors SDA GPIO 8, SCL GPIO 9 (0x40 0x41 0x44 0x45 0x42)
//         relays IN1..IN5 = GPIO 4, 5, 6, 7, 15 (LOW = ON)
//         all GNDs common (SMPS V-, ESP32, sensors, relay board)
//
// Serial Monitor (115200):
//   1..5  toggle that relay; when a relay switches OFF, that channel's
//         in-rush peak and hold average are printed
//   0     all relays off
//   p     live table on/off (current + supply voltage per channel)

#include <Wire.h>

#define NCH 5
#define SHUNT_OHM 0.1f
const uint8_t ADDR[NCH] = {0x40, 0x41, 0x44, 0x45, 0x42};
const int RELAY[NCH] = {4, 5, 6, 7, 15};

// shunt + bus, 16 V range, +-320 mV, 12-bit, continuous
const uint16_t CFG = (3 << 11) | (3 << 7) | (3 << 3) | 7;

bool present[NCH], relayOn[NCH];
uint32_t onAt[NCH];
float peakA[NCH], liveA[NCH], busV[NCH];
double holdSum[NCH];
uint32_t holdN[NCH];
bool showLive = true;

// ---------------- INA219 ----------------
bool wr16(uint8_t a, uint8_t reg, uint16_t v) {
  Wire.beginTransmission(a);
  Wire.write(reg); Wire.write(v >> 8); Wire.write(v & 0xFF);
  return Wire.endTransmission() == 0;
}

bool rd16(uint8_t a, uint8_t reg, uint16_t *v) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)a, 2) != 2) return false;
  *v = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

// ---------------- relays ----------------
void setRelay(int i, bool on) {
  digitalWrite(RELAY[i], on ? LOW : HIGH);
  if (on && !relayOn[i]) {          // new ON: start measuring
    onAt[i] = millis();
    peakA[i] = 0;
    holdSum[i] = 0;
    holdN[i] = 0;
  }
  if (!on && relayOn[i]) {          // OFF: print the result
    float dur = (millis() - onAt[i]) / 1000.0f;
    Serial.printf("\nCH%d result: in-rush peak %.3f A, ", i + 1, peakA[i]);
    if (holdN[i]) Serial.printf("hold avg %.3f A", holdSum[i] / holdN[i]);
    else Serial.print("hold avg -- (keep ON longer than 1 s)");
    Serial.printf(", on for %.1f s\n\n", dur);
  }
  relayOn[i] = on;
  Serial.printf("CH%d relay %s\n", i + 1, on ? "ON" : "OFF");
}

// ---------------- main ----------------
void setup() {
  for (int i = 0; i < NCH; i++) {
    digitalWrite(RELAY[i], HIGH);  // OFF before the pin becomes an output
    pinMode(RELAY[i], OUTPUT);
  }
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nBench test: relays + sensors");

  Wire.begin(8, 9, 400000);
  Wire.setTimeOut(20);
  for (int i = 0; i < NCH; i++) {
    present[i] = wr16(ADDR[i], 0x00, 0x8000);  // reset
    delay(1);
    present[i] = present[i] && wr16(ADDR[i], 0x00, CFG);
    Serial.printf("CH%d  0x%02X  %s\n", i + 1, ADDR[i], present[i] ? "OK" : "NOT FOUND");
  }
  Serial.println("Commands: 1..5 = toggle relay, 0 = all off, p = live table on/off");
}

void loop() {
  // Commands
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '5') {
      setRelay(c - '1', !relayOn[c - '1']);
    } else if (c == '0') {
      for (int i = 0; i < NCH; i++) {
        if (relayOn[i]) setRelay(i, false);
      }
    } else if (c == 'p') {
      showLive = !showLive;
    }
  }

  // Measure (as fast as the bus allows: ~1 ms per round)
  for (int i = 0; i < NCH; i++) {
    if (!present[i]) continue;
    uint16_t sh;
    if (!rd16(ADDR[i], 0x01, &sh)) continue;
    float a = (int16_t)sh * 10e-6f / SHUNT_OHM;
    liveA[i] = a;
    if (relayOn[i]) {
      uint32_t t = millis() - onAt[i];
      if (t < 1000 && a > peakA[i]) peakA[i] = a;  // in-rush window
      if (t >= 1000) { holdSum[i] += a; holdN[i]++; }  // hold average
    }
  }

  // Live table every 300 ms (bus voltage read here)
  static uint32_t last = 0;
  if (showLive && millis() - last >= 300) {
    last = millis();
    for (int i = 0; i < NCH; i++) {
      uint16_t b;
      if (present[i] && rd16(ADDR[i], 0x02, &b)) busV[i] = (b >> 3) * 0.004f;
      if (present[i])
        Serial.printf("CH%d %s %6.3fA %5.2fV | ", i + 1, relayOn[i] ? "ON " : "off", liveA[i], busV[i]);
      else
        Serial.printf("CH%d  -- not found -- | ", i + 1);
    }
    Serial.println();
  }
}
