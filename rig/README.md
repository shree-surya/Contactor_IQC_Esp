# Rig sub-board (ESP32-S3-DevKitC-1)

Step 1: read the 5 INA219 current sensors and print them on the Serial Monitor.
Relays, continuity inputs and the link to the HMI panel are added next.

## Wiring: logic side (all at 3.3 V)

INA219 modules have two address jumpers (A0, A1), so one I2C bus can hold only 4 of
them. The ESP32-S3 has two I2C controllers: CH1–CH4 go on bus A, CH5 on bus B.

| INA219 pin | CH1–CH4 (bus A) | CH5 (bus B) |
|---|---|---|
| VCC | ESP32 **3V3** (not 5V) | ESP32 **3V3** |
| GND | ESP32 GND | ESP32 GND |
| SDA | GPIO **8** | GPIO **17** |
| SCL | GPIO **9** | GPIO **18** |

Address jumpers (solder bridge on the back of the module):

| Channel | Bus | Jumper | Address |
|---|---|---|---|
| CH1 | A | none | 0x40 |
| CH2 | A | A0 | 0x41 |
| CH3 | A | A1 | 0x44 |
| CH4 | A | A0 + A1 | 0x45 |
| CH5 | B | none | 0x40 |

The modules already have I2C pull-up resistors, so no extra resistors are needed.

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
2. Plug the USB cable into the port marked **UART** (or **COM**) on the DevKitC.
3. Press **Upload** (→ in the bottom bar), then the **Serial Monitor** (plug icon).

You should see:

```
Bus A (SDA 8, SCL 9)  : 0x40 0x41 0x44 0x45
Bus B (SDA 17, SCL 18): 0x40
CH1  bus A  0x40  OK
...
      CH1            CH2            CH3            CH4            CH5
 0.000A 12.01V   0.000A 12.02V   ...
```

Type in the Serial Monitor input box and press Enter:

| Key | Action |
|---|---|
| `s` | scan both buses again |
| `1`…`5` | **peak test** on that channel: switch the coil on within 1 s; prints peak, average, samples/s |
| `p` | pause / resume the live table |

`!` after a voltage means the INA219 reported an overflow (current above the shunt range).

## Troubleshooting

- **"nothing found"**: VCC must be 3.3 V, SDA/SCL not swapped, GND connected.
- **Two modules on the same address**: check the A0/A1 jumpers against the table.
- **Negative current**: VIN+ and VIN− are swapped.
- **Bus voltage 0 V with current flowing**: the 12 V ground is not joined to the ESP32 ground.
