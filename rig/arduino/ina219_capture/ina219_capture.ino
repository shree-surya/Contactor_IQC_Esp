// INA219 capture test - one sensor at a time
// Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, cable in "USB Native"
//
// 1. Finds the sensor (GPIO 8/9 or 17/18, address 0x40..0x4F)
// 2. Says READY - switch the coil power ON
// 3. Recording starts by itself when current flows (in-rush captured from the start)
// 4. Type s + Enter to stop: shows in-rush peak, continuous current and the in-rush curve
//    (after 1 s the sensor switches to 128-sample hardware averaging, so a PWM /
//     economizer coil current is averaged correctly)
// 5. Type r + Enter to re-arm (or swap the sensor first: it is detected again)

#include <Wire.h>

#define SHUNT_OHM 0.1f       // 0.05f after fitting an R050 shunt
#define TRIGGER_A 0.020f     // recording starts above 20 mA
#define INRUSH_MS 1000       // in-rush window (stored at full speed)
#define SETTLE_MS 2000       // continuous average starts here (until s, or power off)
#define MAX_SAMPLES 20000    // ~1 s at full speed

TwoWire *bus = nullptr;
uint8_t addr = 0;
int16_t buf[MAX_SAMPLES];    // raw shunt readings of the in-rush window
uint32_t nBuf = 0;
uint32_t t0 = 0;             // millis() when current started
double contSum = 0;          // continuous window (hardware-averaged readings)
uint32_t contN = 0;
float contMin = 0, contMax = 0;
double busSum = 0;           // bus voltage during the hold phase
float busMin = 0;
uint32_t offAt = 0;          // millis() after t0 when the current stopped (0 = still on)
bool avgMode = false;
enum State { NO_SENSOR, ARMED, RECORDING, DONE } state = NO_SENSOR;

// ---------------- INA219 ----------------
// Fast: shunt only, 9-bit (84 us per conversion), +-320 mV, continuous
const uint16_t CFG_FAST = (3 << 11) | (0 << 3) | 5;
// Normal: shunt + bus, 12-bit, 16 V range
const uint16_t CFG_NORMAL = (3 << 11) | (3 << 7) | (3 << 3) | 7;
// Averaging: shunt + bus, each 128 samples (68 ms) - a true average of a PWM
// (economizer) coil current; one result every ~137 ms
const uint16_t CFG_AVG = (3 << 11) | (15 << 7) | (15 << 3) | 7;

bool wr16(uint8_t reg, uint16_t v) {
  bus->beginTransmission(addr);
  bus->write(reg); bus->write(v >> 8); bus->write(v & 0xFF);
  return bus->endTransmission() == 0;
}

bool rd16(uint8_t reg, uint16_t *v) {
  bus->beginTransmission(addr);
  bus->write(reg);
  if (bus->endTransmission(false) != 0) return false;
  if (bus->requestFrom((int)addr, 2) != 2) return false;
  *v = ((uint16_t)bus->read() << 8) | bus->read();
  return true;
}

inline float toAmps(int16_t raw) { return raw * 10e-6f / SHUNT_OHM; }

bool readRaw(int16_t *raw) {
  uint16_t v;
  if (!rd16(0x01, &v)) return false;
  *raw = (int16_t)v;
  return true;
}

// ---------------- setup / detect ----------------
bool findOn(TwoWire &w, int sda, int scl, const char *name) {
  w.begin(sda, scl, 400000);
  w.setTimeOut(20);
  for (uint8_t a = 0x40; a <= 0x4F; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) {
      bus = &w;
      addr = a;
      Serial.printf("Sensor found on %s at address 0x%02X\n", name, a);
      return true;
    }
  }
  return false;
}

void arm() {
  bus = nullptr;
  addr = 0;
  if (!findOn(Wire, 8, 9, "bus A (GPIO 8/9)") && !findOn(Wire1, 17, 18, "bus B (GPIO 17/18)")) {
    Serial.println("No INA219 found on GPIO 8/9 or 17/18 - check wiring, then type r");
    state = NO_SENSOR;
    return;
  }
  wr16(0x00, 0x8000);  // reset
  delay(2);
  wr16(0x00, CFG_FAST);
  delay(2);

  int16_t raw = 0;
  readRaw(&raw);
  if (fabsf(toAmps(raw)) > TRIGGER_A) {
    Serial.printf("Current already flowing (%.3f A) - switch the power OFF, then type r\n", toAmps(raw));
    state = NO_SENSOR;
    return;
  }
  nBuf = 0;
  contSum = 0;
  contN = 0;
  busSum = 0;
  offAt = 0;
  avgMode = false;
  state = ARMED;
  Serial.println("\n>>> READY - switch the coil power ON now <<<");
  Serial.println("    (type s + Enter to stop and show the results)");
}

// ---------------- results ----------------
void report() {
  wr16(0x00, CFG_FAST);
  uint32_t dur = offAt ? offAt : millis() - t0;
  float rate = nBuf * 1000.0f / INRUSH_MS;
  int16_t pk = 0;
  uint32_t pkIdx = 0;
  for (uint32_t i = 0; i < nBuf; i++) {
    if (buf[i] > pk) { pk = buf[i]; pkIdx = i; }
  }
  float peakA = toAmps(pk);
  float pkMs = nBuf ? pkIdx * (float)INRUSH_MS / nBuf : 0;
  bool overRange = pk >= 31900;  // 9-bit reading saturates at ~ +-32000

  Serial.println("\n================ RESULT ================");
  Serial.printf("Sensor          : 0x%02X on %s\n", addr, bus == &Wire ? "bus A (8/9)" : "bus B (17/18)");
  Serial.printf("Recorded        : %.1f s   (%.0f samples/s in the in-rush window)\n", dur / 1000.0f, rate);
  Serial.printf("In-rush peak    : %.3f A  at %.1f ms%s\n", peakA, pkMs,
                overRange ? "   <-- OVER RANGE (shunt too big for this peak)" : "");
  if (contN) {
    Serial.printf("Continuous avg  : %.3f A  (min %.3f, max %.3f, %lu readings, %d ms to %.1f s)\n",
                  contSum / contN, contMin, contMax, (unsigned long)contN, SETTLE_MS, dur / 1000.0f);
    Serial.printf("Supply (hold)   : %.2f V avg, %.2f V min\n", busSum / contN, busMin);
  } else {
    Serial.printf("Continuous avg  : -- (power on for less than %d ms, run longer)\n", SETTLE_MS);
  }
  if (offAt) Serial.printf("Power went off  : at %.1f s (readings after that ignored)\n", offAt / 1000.0f);

  Serial.println("\nIn-rush curve (max per 25 ms):");
  for (int w = 0; w < INRUSH_MS / 25 && nBuf; w++) {
    uint32_t a = (uint32_t)((uint64_t)w * nBuf * 25 / INRUSH_MS), e = (uint32_t)((uint64_t)(w + 1) * nBuf * 25 / INRUSH_MS);
    int16_t m = -32768;
    for (uint32_t i = a; i < e && i < nBuf; i++) m = max(m, buf[i]);
    float amps = toAmps(m);
    int bars = constrain((int)(amps / (0.32f / SHUNT_OHM) * 40), 0, 40);
    Serial.printf("%4d ms %6.3f A |", w * 25, amps);
    for (int k = 0; k < bars; k++) Serial.print("#");
    Serial.println();
  }
  Serial.println("========================================");
  Serial.println("Switch the power OFF, then type r + Enter for the next test");
}

// ---------------- main ----------------
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nINA219 capture test (one sensor)");
  arm();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') arm();
    if (c == 's' && (state == RECORDING || state == ARMED)) {
      if (state == ARMED) Serial.println("Stopped - no current was detected");
      else report();
      state = DONE;
    }
  }

  int16_t raw;
  if (state == ARMED) {
    if (readRaw(&raw) && toAmps(raw) > TRIGGER_A) {
      t0 = millis();
      nBuf = 0;
      buf[nBuf++] = raw;
      state = RECORDING;
      Serial.println("Current detected - recording... (type s to stop)");
    }
  } else if (state == RECORDING) {
    uint32_t el = millis() - t0;
    if (el < INRUSH_MS) {
      // In-rush: fastest single readings, all stored
      if (readRaw(&raw) && nBuf < MAX_SAMPLES) buf[nBuf++] = raw;
      return;
    }
    if (!avgMode) {
      wr16(0x00, CFG_AVG);  // from here on: hardware-averaged readings
      avgMode = true;
      delay(140);
    }
    // Hold phase: one averaged result about every 137 ms
    static uint32_t lastRead = 0;
    if (millis() - lastRead < 140) return;
    lastRead = millis();
    uint16_t b;
    if (!readRaw(&raw) || !rd16(0x02, &b)) return;
    float a = toAmps(raw), v = (b >> 3) * 0.004f;
    Serial.printf("  t=%5.1f s   %.3f A   %.2f V\n", el / 1000.0f, a, v);

    if (offAt) return;
    if (a < TRIGGER_A) {  // power switched off: stop collecting
      offAt = el;
      Serial.println("  (current stopped - type s for the results)");
      return;
    }
    if (el >= SETTLE_MS) {
      if (!contN) { contMin = contMax = a; busMin = v; }
      contMin = min(contMin, a);
      contMax = max(contMax, a);
      busMin = min(busMin, v);
      contSum += a;
      busSum += v;
      contN++;
    }
  }
}
