#pragma once

// ---------------------------------------------------------------------------
// Firmware-wide constants. Change values here, not scattered in the code.
// ---------------------------------------------------------------------------

#define FW_VERSION "0.4.0"

#define NUM_CH 5             // test channels (contactor slots)
#define MAX_MODELS 8
#define MAX_OPERATORS 10
#define SERIAL_MAX_LEN 32    // free-text serial number length
#define NAME_MAX_LEN 47      // model / operator name length
#define PASS_MAX_LEN 32      // operator password length

#define ADMIN_PASSWORD "100100"

// INA219 with the stock 0.1 ohm shunt saturates at 3.2 A. Any peak at or above
// this is reported as OVER RANGE (FAIL), never PASS. Used by the simulation;
// the real rig board reports its own range (set SHUNT_OHM in its firmware).
#define CURRENT_RANGE_A 3.2f

// Test hardware: 1 = real rig board over I2C (rig/arduino/rig_firmware),
// 0 = built-in simulation (no rig board needed, for UI demos)
#ifndef RIG_LINK
#define RIG_LINK 1
#endif
#define RIG_I2C_ADDR 0x30    // rig board I2C slave address
#define RIG_I2C_HZ 100000    // link speed (cable between the boards)
#define RIG_POLL_MS 40       // status read interval

// Current below this counts as "coil off" for the Open and Release checks.
#define OFF_CURRENT_MAX_A 0.02f

// Max cycles per unit (the Results sheet has columns for this many cycles)
#define MAX_CYCLES 5

// RGB panel pixel clock. 15 MHz = ~31 frames/s. If the picture still
// glitches during Sheet uploads, try 14000000 or 12000000 (less PSRAM load).
#define PANEL_PCLK_HZ 15000000

// Finished units waiting in RAM until loop() moves them to the flash queue
#define RESULT_QUEUE_LEN 10

// CrowPanel 7" V3.0 shared I2C bus (touch controller + PCA9557 expander)
#define PIN_I2C_SDA 19
#define PIN_I2C_SCL 20
#define PCA9557_ADDR 0x18
