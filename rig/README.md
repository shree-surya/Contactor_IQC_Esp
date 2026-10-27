# Rig sub-board (ESP32-S3-DevKitC-1)

Step 1: read the 5 INA219 current sensors and print them on the Serial Monitor.
Relays, continuity inputs and the link to the HMI panel are added next.

## Wiring: logic side (all at 3.3 V)

The ESP32-S3 has **two** I2C controllers:

- **I2C0**: all 5 INA219 sensors (GPIO 8 / 9). The sub-board is the master.
- **I2C1**: reserved for the link to the HMI panel (GPIO 17 / 18). The sub-board is a slave.

All five sensors share one bus, wired in parallel:

| INA219 pin | ESP32-S3 |
|---|---|
| VCC | **3V3** (not 5V) |
| GND | GND |
| SDA | GPIO **8** |
| SCL | GPIO **9** |

Address setting:

| Channel | A0 | A1 | Address |
|---|---|---|---|
| CH1 | open | open | 0x40 |
| CH2 | bridged | open | 0x41 |
| CH3 | open | bridged | 0x44 |
| CH4 | bridged | bridged | 0x45 |
| CH5 | **wire to SDA** | open | 0x42 |

**CH5 (0x42):** leave both jumpers open. Solder a thin wire from the **A0 pad on the chip side**
of the A0 jumper to the module's **SDA** pin. That is the pad *not* connected to VCC: check
with a multimeter in continuity mode. This uses the INA219's "A0 = SDA" address option.
If you'd rather not solder, a TCA9548A I2C multiplexer also works (ask before buying).

Each module has its own 10 kΩ pull-ups. Five in parallel (2 kΩ) is fine for short wires;
keep the sensor bus under ~50 cm in total.

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
