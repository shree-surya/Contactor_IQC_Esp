// Relay board test - 5 channels, active LOW inputs (LOW = relay ON)
// Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled, cable in "USB Native"
// Serial Monitor: 1..5 = toggle that relay, 0 = all OFF

const int RELAY[5] = {4, 5, 6, 7, 15};  // IN1..IN5
bool on[5] = {false};

void setRelay(int i, bool st) {
  on[i] = st;
  digitalWrite(RELAY[i], st ? LOW : HIGH);
}

void show() {
  Serial.print("Relays: ");
  for (int i = 0; i < 5; i++) Serial.printf("CH%d=%s  ", i + 1, on[i] ? "ON " : "off");
  Serial.println();
}

void setup() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(RELAY[i], HIGH);  // OFF before the pin becomes an output
    pinMode(RELAY[i], OUTPUT);
  }
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nRelay test: 1..5 = toggle, 0 = all off");
  show();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c >= '1' && c <= '5') {
    int i = c - '1';
    setRelay(i, !on[i]);
    show();
  } else if (c == '0') {
    for (int i = 0; i < 5; i++) setRelay(i, false);
    show();
  }
}
