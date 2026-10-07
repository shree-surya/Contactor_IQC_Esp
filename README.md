# Contactor IQC Rig – ESP32 firmware

Incoming-quality-check rig for 12 V contactors, testing up to 5 contactors per batch.

| Folder | Board | Status |
|---|---|---|
| [`hmi/`](hmi/README.md) | Elecrow CrowPanel 7" ESP32-S3 (DIS08070H V3.0): touch UI, WiFi, Google Sheets | UI + simulated rig ready |
| `rig/` | ESP32-S3-DevKitC-1 sub-board: relays, 5× INA219, continuity inputs | planned |

## Test sequence (per contactor, per cycle)

1. **Open**: contact open and coil current ~0 before switching on
2. **In-rush**: peak coil current in the first second after relay ON
3. **Continuous**: average coil current from 2 s to 5 s
4. **Continuity**: contact must stay closed for the whole 2–5 s window
5. **Release**: relay OFF, after 0.5 s contact open and current ~0

Channels start 1.5 s apart so only one in-rush happens at a time. Each channel stops at its
first failure; the others carry on. Default: 5 cycles with a 5 s gap. Limits and timings per model.

The original Raspberry Pi version lives in [shree-surya/Contactor_IQC](https://github.com/shree-surya/Contactor_IQC).
