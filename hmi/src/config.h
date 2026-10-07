#pragma once

// ---------------------------------------------------------------------------
// Firmware-wide constants. Change values here, not scattered in the code.
// ---------------------------------------------------------------------------

#define FW_VERSION "0.1.0-ui-sim"

#define NUM_CH 5             // test channels (contactor slots)
#define MAX_MODELS 8
#define MAX_OPERATORS 10
#define SERIAL_MAX_LEN 32    // free-text serial number length
#define NAME_MAX_LEN 47      // model / operator name length

#define ADMIN_PASSWORD "100100"

// INA219 with the stock 0.1 ohm shunt saturates at 3.2 A. Any peak at or above
// this is reported as OVER RANGE (FAIL), never PASS. Raise it only after the
// shunt has been changed (e.g. 6.4 A for 0.05 ohm, 9.6 A for 0.033 ohm).
#define CURRENT_RANGE_A 3.2f

// Current below this counts as "coil off" for the Open and Release checks.
#define OFF_CURRENT_MAX_A 0.02f

// Pending result rows kept in RAM until uploaded (moves to flash later).
#define RESULT_QUEUE_LEN 64

// CrowPanel 7" V3.0 shared I2C bus (touch controller + PCA9557 expander)
#define PIN_I2C_SDA 19
#define PIN_I2C_SCL 20
#define PCA9557_ADDR 0x18
