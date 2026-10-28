// INA219 capture test - all 5 sensors at once (wired in series = same current)
// Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, cable in "USB Native"
//
// All 5 on GPIO 8/9: CH1 0x40, CH2 0x41, CH3 0x44, CH4 0x45, CH5 0x42 (A0 bridged to SDA)
// 1. Checks all 5 sensors, says READY  -> switch the coil power ON
// 2. Recording starts by itself when current flows on any channel
// 3. Type s + Enter to stop: per-channel in-rush peak, hold current, supply
//    voltage, and how far each channel is from the average of all five
// 4. Type r + Enter to re-arm

#include <Wire.h>

#define SHUNT_OHM 0.1f      // 0.05f after fitting R050 shunts
#define TRIGGER_A 0.020f
#define INRUSH_MS 1000
#define SETTLE_MS 2000
#define NCH 5
#define BINS (INRUSH_MS / 25)  // in-rush curve: max per 25 ms

struct Ch { TwoWire *bus; uint8_t addr; bool ok; };
Ch C[NCH] = {
  {&Wire, 0x40, false}, {&Wire, 0x41, false}, {&Wire, 0x44, false}, {&Wire, 0x45, false},
  {&Wire, 0x42, false},
};

const uint16_t CFG_FAST = (3 << 11) | (0 << 3) | 5;                // shunt only, 9-bit, 84 us
const uint16_t CFG_AVG = (3 << 11) | (15 << 7) | (15 << 3) | 7;    // shunt+bus, 128-sample avg

int16_t peak[NCH], bins[NCH][BINS];
uint32_t nFast[NCH];
double holdSum[NCH], busSum[NCH];
float holdMin[NCH], holdMax[NCH];
uint32_t holdN = 0, t0 = 0, offAt = 0;
bool avgMode = false;
enum State { NOT_READY, ARMED, RECORDING, DONE } state = NOT_READY;

// ---------------- INA219 ----------------
bool wr16(Ch &c, uint8_t reg, uint16_t v) {
  c.bus->beginTransmission(c.addr);
  c.bus->write(reg); c.bus->write(v >> 8); c.bus->write(v & 0xFF);
  return c.bus->endTransmission() == 0;
}

bool rd16(Ch &c, uint8_t reg, uint16_t *v) {
  c.bus->beginTransmission(c.addr);
  c.bus->write(reg);
  if (c.bus->endTransmission(false) != 0) return false;
  if (c.bus->requestFrom((int)c.addr, 2) != 2) return false;
  *v = ((uint16_t)c.bus->read() << 8) | c.bus->read();
  return true;
}

bool rdShunt(Ch &c, int16_t *raw) {
  uint16_t v;
  if (!rd16(c, 0x01, &v)) return false;
  *raw = (int16_t)v;
  return true;
}

inline float amps(int16_t raw) { return raw * 10e-6f / SHUNT_OHM; }

void setAll(uint16_t cfg) {
  for (int i = 0; i < NCH; i++) if (C[i].ok) wr16(C[i], 0x00, cfg);
}

// ---------------- arm / report ----------------
void arm() {
  int found = 0;
  for (int i = 0; i < NCH; i++) {
    C[i].ok = wr16(C[i], 0x00, 0x8000);  // reset
    delay(2);
    C[i].ok = C[i].ok && wr16(C[i], 0x00, CFG_FAST);
    Serial.printf("CH%d  0x%02X  %s\n", i + 1, C[i].addr, C[i].ok ? "OK" : "NOT FOUND");
    found += C[i].ok;
  }
  if (!found) {
    Serial.println("No sensors found - check wiring, then type r");
    state = NOT_READY;
    return;
  }
  delay(2);
  for (int i = 0; i < NCH; i++) {
    int16_t raw;
    if (C[i].ok && rdShunt(C[i], &raw) && amps(raw) > TRIGGER_A) {
      Serial.printf("CH%d already shows %.3f A - switch the power OFF, then type r\n", i + 1, amps(raw));
      state = NOT_READY;
      return;
    }
  }
  for (int i = 0; i < NCH; i++) {
    peak[i] = 0;
    nFast[i] = 0;
    holdSum[i] = busSum[i] = 0;
    for (int b = 0; b < BINS; b++) bins[i][b] = 0;
  }
  holdN = 0;
  offAt = 0;
  avgMode = false;
  state = ARMED;
  Serial.printf("\n>>> READY (%d sensor%s) - switch the coil power ON now <<<\n", found, found > 1 ? "s" : "");
  Serial.println("    (type s + Enter to stop and show the results)");
}

void report() {
  setAll(CFG_FAST);
  uint32_t dur = offAt ? offAt : millis() - t0;
  Serial.println("\n======================= RESULT =======================");
  Serial.printf("Recorded %.1f s, hold readings %lu\n\n", dur / 1000.0f, (unsigned long)holdN);
  Serial.println("Ch   In-rush peak   Hold avg (min-max)          Supply   Samples/s");

  float pkMean = 0, hMean = 0;
  int n = 0;
  for (int i = 0; i < NCH; i++) {
    if (!C[i].ok) continue;
    pkMean += amps(peak[i]);
    if (holdN) hMean += holdSum[i] / holdN;
    n++;
  }
  if (n) { pkMean /= n; hMean /= n; }

  for (int i = 0; i < NCH; i++) {
    if (!C[i].ok) { Serial.printf("CH%d  -- not found --\n", i + 1); continue; }
    float pk = amps(peak[i]);
    Serial.printf("CH%d  %6.3f A%s", i + 1, pk, peak[i] >= 31900 ? " OVR" : "    ");
    if (holdN)
      Serial.printf("   %6.3f A (%.3f-%.3f)   %5.2f V", holdSum[i] / holdN, holdMin[i], holdMax[i],
                    busSum[i] / holdN);
    else
      Serial.print("   --                                    ");
    Serial.printf("   %5lu\n", (unsigned long)(nFast[i] * 1000UL / INRUSH_MS));
  }

  Serial.println("\nDeviation from the 5-channel average (same current flows through all):");
  for (int i = 0; i < NCH; i++) {
    if (!C[i].ok) continue;
    float dp = pkMean > 0 ? (amps(peak[i]) - pkMean) / pkMean * 100 : 0;
    float dh = (holdN && hMean > 0) ? (holdSum[i] / holdN - hMean) / hMean * 100 : 0;
    bool bad = fabsf(dp) > 5 || fabsf(dh) > 3;
    Serial.printf("CH%d  peak %+5.1f %%   hold %+5.1f %%   %s\n", i + 1, dp, dh, bad ? "<-- CHECK" : "ok");
  }
  if (offAt) Serial.printf("\nPower went off at %.1f s (later readings ignored)\n", offAt / 1000.0f);
  if (!holdN) Serial.printf("\nKeep the power on longer than %d ms to get the hold current\n", SETTLE_MS);

  Serial.println("\nIn-rush curve, max per 25 ms (A):");
  Serial.println("  ms    CH1    CH2    CH3    CH4    CH5");
  for (int b = 0; b < 12; b++) {  // first 300 ms
    Serial.printf("%4d", b * 25);
    for (int i = 0; i < NCH; i++) {
      if (C[i].ok) Serial.printf("  %5.3f", amps(bins[i][b]));
      else Serial.print("     --");
    }
    Serial.println();
  }
  Serial.println("======================================================");
  Serial.println("Switch the power OFF, then type r + Enter for the next test");
}

// ---------------- main ----------------
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nINA219 capture test - 5 sensors");
  Wire.begin(8, 9, 400000);
  Wire.setTimeOut(20);

  // Show every address on the bus (expect 0x40 0x41 0x42 0x44 0x45)
  Serial.print("Bus scan (GPIO 8/9):");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
  }
  Serial.println();
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
    for (int i = 0; i < NCH; i++) {
      if (C[i].ok && rdShunt(C[i], &raw) && amps(raw) > TRIGGER_A) {
        t0 = millis();
        state = RECORDING;
        Serial.println("Current detected - recording... (type s to stop)");
        break;
      }
    }
    return;
  }
  if (state != RECORDING) return;

  uint32_t el = millis() - t0;
  if (el < INRUSH_MS) {
    // In-rush: read all channels round-robin as fast as possible
    int b = el / 25;
    for (int i = 0; i < NCH; i++) {
      if (!C[i].ok || !rdShunt(C[i], &raw)) continue;
      nFast[i]++;
      if (raw > peak[i]) peak[i] = raw;
      if (raw > bins[i][b]) bins[i][b] = raw;
    }
    return;
  }

  if (!avgMode) {
    setAll(CFG_AVG);  // hold phase: 128-sample hardware averaging
    avgMode = true;
    delay(140);
  }
  static uint32_t lastRead = 0;
  if (millis() - lastRead < 140) return;
  lastRead = millis();

  float a[NCH], v[NCH];
  bool anyOn = false;
  Serial.printf("  t=%5.1f s ", el / 1000.0f);
  for (int i = 0; i < NCH; i++) {
    uint16_t bv;
    a[i] = v[i] = 0;
    if (C[i].ok && rdShunt(C[i], &raw) && rd16(C[i], 0x02, &bv)) {
      a[i] = amps(raw);
      v[i] = (bv >> 3) * 0.004f;
      Serial.printf(" %6.3fA %5.2fV", a[i], v[i]);
      if (a[i] > TRIGGER_A) anyOn = true;
    } else {
      Serial.print("    --     --  ");
    }
  }
  Serial.println();

  if (offAt) return;
  if (!anyOn) {
    offAt = el;
    Serial.println("  (current stopped - type s for the results)");
    return;
  }
  if (el < SETTLE_MS) return;
  for (int i = 0; i < NCH; i++) {
    if (!C[i].ok) continue;
    if (!holdN) holdMin[i] = holdMax[i] = a[i];
    holdMin[i] = min(holdMin[i], a[i]);
    holdMax[i] = max(holdMax[i], a[i]);
    holdSum[i] += a[i];
    busSum[i] += v[i];
  }
  holdN++;
}
