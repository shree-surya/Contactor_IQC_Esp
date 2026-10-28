// Contactor IQC - rig board firmware (ESP32-S3)
// Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, cable in "USB Native"
//
// What it does:
//   - reads 5 INA219 current sensors continuously (in-rush peak + average)
//   - drives 5 relays (active LOW relay board)
//   - talks to the HMI panel as an I2C slave (address 0x30)
// The HMI runs the test sequence and decides PASS/FAIL. If the HMI stops
// talking for 1.5 s, all relays switch OFF (safety).
//
// Wiring:
//   Sensors (bus A):  SDA GPIO 8, SCL GPIO 9, VCC 3V3, GND
//                     CH1 0x40, CH2 0x41 (A0), CH3 0x44 (A1), CH4 0x45 (A0+A1),
//                     CH5 0x42 (A0 wired to SDA)
//   HMI link (bus B): GPIO 17 = SDA, GPIO 18 = SCL, GND  -> CrowPanel I2C header
//   Relays IN1..IN5:  GPIO 4, 5, 6, 7, 15   (LOW = relay ON)
//
// Serial Monitor (115200): p = print live table on/off, 1..5 = toggle relay
// (bench use only), 0 = all relays off

#include <Wire.h>

// ---------------- settings ----------------
#define NCH 5
#define SHUNT_OHM 0.1f        // 0.05f after fitting R050 shunts
#define SENS_SDA 8
#define SENS_SCL 9
#define LINK_SDA 17
#define LINK_SCL 18
#define LINK_ADDR 0x30
#define LINK_TIMEOUT_MS 1500  // no HMI traffic for this long -> relays OFF

const uint8_t SENS_ADDR[NCH] = {0x40, 0x41, 0x44, 0x45, 0x42};
const int RELAY_PIN[NCH] = {4, 5, 6, 7, 15};

// INA219: 16 V, +-320 mV, shunt only, 12-bit (532 us), continuous
const uint16_t INA_CFG = (3 << 11) | (3 << 3) | 5;

// ---------------- shared state (sampling loop <-> I2C slave callbacks) ----------------
struct ChStats {
  bool present;
  int16_t live;      // mA, average of the last samples
  int16_t peak;      // mA since last reset
  int32_t sum;       // for the average since last reset (mA)
  uint32_t n;
  uint8_t resets;    // incremented on every reset (HMI uses it to know data is fresh)
  bool overRange;
};

ChStats st[NCH];
uint8_t relayMask = 0;
volatile uint32_t lastMasterMs = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool printTable = false;

// ---------------- relays ----------------
void applyRelays(uint8_t mask) {
  relayMask = mask & 0x1F;
  for (int i = 0; i < NCH; i++) digitalWrite(RELAY_PIN[i], (relayMask >> i) & 1 ? LOW : HIGH);
}

// ---------------- INA219 ----------------
bool inaWrite(uint8_t addr, uint8_t reg, uint16_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg); Wire.write(v >> 8); Wire.write(v & 0xFF);
  return Wire.endTransmission() == 0;
}

bool inaShunt(uint8_t addr, int16_t *raw) {
  Wire.beginTransmission(addr);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 2) != 2) return false;
  *raw = (int16_t)(((uint16_t)Wire.read() << 8) | Wire.read());
  return true;
}

void detectSensors() {
  for (int i = 0; i < NCH; i++) {
    bool ok = inaWrite(SENS_ADDR[i], 0x00, 0x8000);  // reset
    delay(1);
    ok = ok && inaWrite(SENS_ADDR[i], 0x00, INA_CFG);
    portENTER_CRITICAL(&mux);
    st[i].present = ok;
    portEXIT_CRITICAL(&mux);
  }
}

// ---------------- I2C slave (HMI link) ----------------
// HMI -> rig:  [0x01, relayMask, resetMask]  set relays, reset stats of the
//                                             channels in resetMask
// rig -> HMI:  47-byte status packet (see buildPacket)
#define PKT_LEN (6 + NCH * 8 + 1)

void onLinkReceive(int) {
  uint8_t b[8];
  int n = 0;
  while (Wire1.available() && n < (int)sizeof(b)) b[n++] = Wire1.read();
  while (Wire1.available()) Wire1.read();
  lastMasterMs = millis();
  if (n >= 3 && b[0] == 0x01) {
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < NCH; i++) {
      if ((b[2] >> i) & 1) {
        st[i].peak = 0;
        st[i].sum = 0;
        st[i].n = 0;
        st[i].overRange = false;
        st[i].resets++;
      }
    }
    portEXIT_CRITICAL(&mux);
    applyRelays(b[1]);
  }
}

void onLinkRequest() {
  uint8_t p[PKT_LEN];
  uint8_t present = 0;
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < NCH; i++) if (st[i].present) present |= 1 << i;
  p[0] = 0xA5;                                   // magic
  p[1] = 1;                                      // protocol version
  p[2] = relayMask;
  p[3] = present;
  p[4] = (uint8_t)(0.32f / SHUNT_OHM * 10 + 0.5f);  // measuring range x10 A
  p[5] = 0;
  for (int i = 0; i < NCH; i++) {
    uint8_t *c = p + 6 + i * 8;
    int16_t avg = st[i].n ? (int16_t)(st[i].sum / (int32_t)st[i].n) : 0;
    c[0] = st[i].live & 0xFF;  c[1] = st[i].live >> 8;
    c[2] = st[i].peak & 0xFF;  c[3] = st[i].peak >> 8;
    c[4] = avg & 0xFF;         c[5] = avg >> 8;
    c[6] = st[i].resets;
    c[7] = (st[i].overRange ? 1 : 0) | (st[i].n ? 2 : 0);
  }
  portEXIT_CRITICAL(&mux);
  uint8_t sum = 0;
  for (int i = 0; i < PKT_LEN - 1; i++) sum += p[i];
  p[PKT_LEN - 1] = sum;
  Wire1.write(p, PKT_LEN);
  lastMasterMs = millis();
}

// ---------------- main ----------------
void setup() {
  for (int i = 0; i < NCH; i++) {
    digitalWrite(RELAY_PIN[i], HIGH);  // OFF before becoming an output
    pinMode(RELAY_PIN[i], OUTPUT);
  }
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nContactor IQC rig board");

  Wire.begin(SENS_SDA, SENS_SCL, 400000);
  Wire.setTimeOut(20);
  detectSensors();
  for (int i = 0; i < NCH; i++)
    Serial.printf("CH%d  0x%02X  %s\n", i + 1, SENS_ADDR[i], st[i].present ? "OK" : "NOT FOUND");

  Wire1.onReceive(onLinkReceive);
  Wire1.onRequest(onLinkRequest);
  Wire1.begin((uint8_t)LINK_ADDR, LINK_SDA, LINK_SCL, 100000);
  Serial.printf("HMI link: I2C slave 0x%02X on GPIO %d/%d\n", LINK_ADDR, LINK_SDA, LINK_SCL);
  Serial.println("Commands: p = live table, 1..5 = toggle relay, 0 = all off");
}

void loop() {
  // 1) Sample every sensor (one round = ~5 x 0.1 ms)
  static int16_t hist[NCH][8];
  static uint8_t hpos = 0;
  for (int i = 0; i < NCH; i++) {
    if (!st[i].present) continue;
    int16_t raw;
    if (!inaShunt(SENS_ADDR[i], &raw)) continue;
    int16_t mA = (int16_t)(raw * 10e-3f / SHUNT_OHM);  // raw LSB 10 uV
    hist[i][hpos] = mA;
    int32_t s = 0;
    for (int k = 0; k < 8; k++) s += hist[i][k];
    portENTER_CRITICAL(&mux);
    st[i].live = s / 8;
    if (mA > st[i].peak) st[i].peak = mA;
    if (raw >= 31900) st[i].overRange = true;
    st[i].sum += mA;
    st[i].n++;
    portEXIT_CRITICAL(&mux);
  }
  hpos = (hpos + 1) & 7;

  // 2) Safety: HMI silent -> relays off
  if (relayMask && millis() - lastMasterMs > LINK_TIMEOUT_MS) {
    applyRelays(0);
    Serial.println("HMI link timeout - all relays OFF");
  }

  // 3) Re-check missing sensors every 2 s
  static uint32_t lastDetect = 0;
  if (millis() - lastDetect > 2000) {
    lastDetect = millis();
    bool missing = false;
    for (int i = 0; i < NCH; i++) missing |= !st[i].present;
    if (missing) detectSensors();
  }

  // 4) Serial monitor (bench use)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'p') printTable = !printTable;
    else if (c >= '1' && c <= '5') { applyRelays(relayMask ^ (1 << (c - '1'))); lastMasterMs = millis(); }
    else if (c == '0') applyRelays(0);
  }
  static uint32_t lastPrint = 0;
  if (printTable && millis() - lastPrint > 300) {
    lastPrint = millis();
    for (int i = 0; i < NCH; i++) {
      if (st[i].present) Serial.printf("CH%d %s %6.3fA  ", i + 1, (relayMask >> i) & 1 ? "ON " : "off", st[i].live / 1000.0f);
      else Serial.printf("CH%d  --         ", i + 1);
    }
    Serial.printf(" link %s\n", millis() - lastMasterMs < LINK_TIMEOUT_MS ? "OK" : "idle");
  }
}
