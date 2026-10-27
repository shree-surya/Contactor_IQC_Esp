// Contactor IQC rig - INA219 test (single-file Arduino IDE version)
// Board: ESP32-S3 Dev Module, USB CDC On Boot: Disabled, port = "USB for UART"
//
// Reads 5 INA219s and prints current + bus voltage 5x per second.
// Serial Monitor (115200): s = scan, 1..5 = 1 s peak test, p = pause

#include <Wire.h>

// ---------------- settings ----------------
#define BUS_A_SDA 8     // CH1..CH4
#define BUS_A_SCL 9
#define BUS_B_SDA 17    // CH5
#define BUS_B_SCL 18
#define I2C_HZ 100000   // 100 kHz: tolerant of breadboard wiring
#define SHUNT_OHM 0.1f  // 0.05f after fitting an R050 shunt

struct Sensor { TwoWire *bus; uint8_t addr; bool ok; };
Sensor S[5] = {
  {&Wire,  0x40, false},  // CH1: no jumper
  {&Wire,  0x41, false},  // CH2: A0 bridged
  {&Wire,  0x44, false},  // CH3: A1 bridged
  {&Wire,  0x45, false},  // CH4: A0 + A1 bridged
  {&Wire1, 0x40, false},  // CH5: bus B, no jumper
};

// INA219 config: 16 V range, +-320 mV shunt, 12-bit, continuous shunt + bus
const uint16_t CFG_NORMAL = (3 << 11) | (3 << 7) | (3 << 3) | 7;
// fast: shunt only, 9-bit, new value every 84 us (for in-rush peaks)
const uint16_t CFG_FAST = (3 << 11) | (0 << 3) | 5;
bool paused = false;

// ---------------- INA219 access ----------------
bool wr16(Sensor &s, uint8_t reg, uint16_t v) {
  s.bus->beginTransmission(s.addr);
  s.bus->write(reg);
  s.bus->write(v >> 8);
  s.bus->write(v & 0xFF);
  return s.bus->endTransmission() == 0;
}

bool rd16(Sensor &s, uint8_t reg, uint16_t *v) {
  s.bus->beginTransmission(s.addr);
  s.bus->write(reg);
  if (s.bus->endTransmission(false) != 0) return false;
  if (s.bus->requestFrom((int)s.addr, 2) != 2) return false;
  *v = ((uint16_t)s.bus->read() << 8) | s.bus->read();
  return true;
}

bool readAmps(Sensor &s, float *a) {
  uint16_t r;
  if (!rd16(s, 0x01, &r)) return false;
  *a = (int16_t)r * 10e-6f / SHUNT_OHM;  // shunt LSB = 10 uV
  return true;
}

bool readVolts(Sensor &s, float *v, bool *ovf) {
  uint16_t r;
  if (!rd16(s, 0x02, &r)) return false;
  *ovf = r & 1;
  *v = (r >> 3) * 0.004f;  // bus LSB = 4 mV
  return true;
}

// ---------------- helpers ----------------
void scan(TwoWire &w, const char *name) {
  Serial.printf("%s:", name);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) { Serial.printf(" 0x%02X", a); n++; }
  }
  Serial.println(n ? "" : " nothing found - check wiring / 3.3 V");
}

void initSensors() {
  for (int i = 0; i < 5; i++) {
    S[i].ok = wr16(S[i], 0x00, 0x8000);  // reset
    delay(1);
    S[i].ok = S[i].ok && wr16(S[i], 0x00, CFG_NORMAL);
    Serial.printf("CH%d  bus %c  0x%02X  %s\n", i + 1, S[i].bus == &Wire ? 'A' : 'B', S[i].addr,
                  S[i].ok ? "OK" : "NOT FOUND");
  }
}

void printRow() {
  static uint32_t lines = 0;
  if (lines++ % 20 == 0)
    Serial.println("\n      CH1            CH2            CH3            CH4            CH5");
  for (int i = 0; i < 5; i++) {
    float a, v; bool ovf;
    if (S[i].ok && readAmps(S[i], &a) && readVolts(S[i], &v, &ovf))
      Serial.printf("%6.3fA %5.2fV%s", a, v, ovf ? "!" : " ");
    else
      Serial.print("   --  --      ");
  }
  Serial.println();
}

void peakTest(int i) {
  if (!S[i].ok) { Serial.printf("CH%d not found\n", i + 1); return; }
  Serial.printf("CH%d peak test: switch the coil ON now (1 s)...\n", i + 1);
  wr16(S[i], 0x00, CFG_FAST);
  float peak = 0, sum = 0, a;
  uint32_t n = 0, t0 = millis();
  while (millis() - t0 < 1000) {
    if (readAmps(S[i], &a)) { if (a > peak) peak = a; sum += a; n++; }
  }
  wr16(S[i], 0x00, CFG_NORMAL);
  Serial.printf("CH%d  peak %.3f A   avg %.3f A   %lu samples/s%s\n", i + 1, peak, n ? sum / n : 0,
                (unsigned long)n, peak > 0.98f * (0.32f / SHUNT_OHM) ? "   OVER RANGE" : "");
}

// ---------------- main ----------------
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nContactor IQC rig - INA219 test (Arduino)");

  Wire.begin(BUS_A_SDA, BUS_A_SCL, I2C_HZ);
  Wire.setTimeOut(20);   // a stuck bus fails fast instead of freezing
  Wire1.begin(BUS_B_SDA, BUS_B_SCL, I2C_HZ);
  Wire1.setTimeOut(20);

  scan(Wire, "Bus A (SDA 8, SCL 9)  ");
  scan(Wire1, "Bus B (SDA 17, SCL 18)");
  initSensors();
  Serial.println("Commands: s = scan, 1..5 = peak test, p = pause");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's') {
      scan(Wire, "Bus A");
      scan(Wire1, "Bus B");
      initSensors();
    } else if (c >= '1' && c <= '5') {
      peakTest(c - '1');
    } else if (c == 'p') {
      paused = !paused;
    }
  }
  static uint32_t last = 0;
  if (!paused && millis() - last >= 200) {
    last = millis();
    printRow();
  }
}
