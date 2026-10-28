# Rig board (ESP32-S3)

## Firmware for the real rig: `arduino/rig_firmware/rig_firmware.ino`

Reads the 5 INA219s, drives the 5 relays and answers the HMI panel over I2C
(slave address 0x30). The HMI runs the test sequence. If the HMI stops talking
for 1.5 s, all relays switch off.

| Function | ESP32-S3 pins |
|---|---|
| Sensors CH1..CH5 (0x40, 0x41, 0x44, 0x45, **0x42**) | SDA **GPIO 8**, SCL **GPIO 9**, 3V3, GND |
| Link to the HMI panel | SDA **GPIO 17**, SCL **GPIO 18**, GND |
| Relays IN1..IN5 (LOW = ON) | GPIO **4, 5, 6, 7, 15** |
| Feedback contacts CH1..CH5 (4-pin contactors) | GPIO **1, 2, 10, 11, 12** |

CH5 must be at **0x42** on GPIO 8/9 (A0 wired to SDA), because GPIO 17/18 are
now the HMI link. Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot: Enabled,
cable in the **USB Native** port.

**Feedback contact (continuity) wiring**, 4-pin contactors only: one feedback pin to the
channel's GPIO, the other feedback pin to ESP32 GND. Closed contact = LOW. The input uses
the internal pull-up; a 2.2 kΩ pull-up from the GPIO to 3V3 gives the contact a stronger
current (about 1.5 mA) and better noise immunity on long wires. First check with a
multimeter that the feedback pins have no connection to the coil pins. Set
**Continuity = Yes** for that model in the Specs sheet; 2-pin models stay **No** and show
-NA-. Serial Monitor `p` shows each contact as CLOSE / open.

**HMI ↔ rig board cable** (CrowPanel I2C header → rig board):
SDA IO19 → GPIO 17, SCL IO20 → GPIO 18, GND → GND. Do not connect the power
pins; each board has its own supply. Keep the cable under ~50 cm.

## Bench / test sketches (`arduino/`)

| Sketch | Use |
|---|---|
| `ina219_capture` | one sensor: auto-detect, in-rush + hold report |
| `ina219_capture5` | all 5 sensors in series: compares channels |
| `relay_test` | toggle relays 1..5 from the Serial Monitor |
| `rig_ina219_test` | live table of all 5 sensors |

The PlatformIO project in this folder (`src/`) is the early INA219 bench test.

## Wiring: coil side (12 V)

Each INA219 sits in the **+12 V line** of one contactor coil (high side):

```
+12 V ──► VIN+ [INA219] VIN− ──► relay COM ;  relay NO ──► coil +
coil − ──► 12 V GND
12 V GND ──── ESP32 GND   (common ground is required)
```

- Bus voltage is measured at VIN− against GND, so the 12 V ground must be joined to the
  ESP32 ground.
- Stock modules have a **0.1 Ω (R100)** shunt: maximum **3.2 A**. Models with in-rush up to
  3.3 A need a **0.05 Ω (R050)** shunt (6.4 A). Change `SHUNT_OHM` in `src/config.h` to match.
- Until the relays are wired you can test with any 12 V load (a coil, a lamp, or a power
  resistor) switched by hand.

## Flash and run

1. VS Code → **File → Open Folder…** → select `rig`.
2. Plug the USB cable into the **USB Native** port. On the 7Semi board the "USB for UART"
   port holds the chip in reset while a Serial Monitor is open (LED freezes, no output).
   Arduino IDE: board **ESP32S3 Dev Module**, **USB CDC On Boot: Enabled**, Flash 8MB,
   PSRAM OPI. Single-file sketch: `arduino/rig_ina219_test/rig_ina219_test.ino`.
   If an upload can't connect: hold BOOT, tap RST, release BOOT, upload again.
3. Press **Upload** (→ in the bottom bar), then the **Serial Monitor** (plug icon).

You should see:

```
Sensor bus (SDA 8, SCL 9): 0x40 0x41 0x42 0x44 0x45
CH1  0x40  OK
...
      CH1            CH2            CH3            CH4            CH5
 0.000A 12.01V   0.000A 12.02V   ...
```

Type in the Serial Monitor input box and press Enter:

| Key | Action |
|---|---|
| `s` | scan the sensor bus again |
| `1`…`5` | **peak test** on that channel: switch the coil on within 1 s; prints peak, average, samples/s |
| `p` | pause / resume the live table |

`!` after a voltage means the INA219 reported an overflow (current above the shunt range).

## Troubleshooting

- **"nothing found"**: VCC must be 3.3 V, SDA/SCL not swapped, GND connected.
- **Two modules on the same address**: check the A0/A1 jumpers against the table.
- **Negative current**: VIN+ and VIN− are swapped.
- **Bus voltage 0 V with current flowing**: the 12 V ground is not joined to the ESP32 ground.
