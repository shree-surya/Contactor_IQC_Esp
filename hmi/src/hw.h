#pragma once
#include <stdint.h>
#include "app_data.h"

// ---------------------------------------------------------------------------
// Test hardware used by the sequencer (rig.cpp). Two implementations:
//   hw_link.cpp  real rig board (ESP32-S3 + INA219s + relays) over I2C   RIG_LINK 1
//   hw_sim.cpp   simulated relays / currents / contacts                    RIG_LINK 0
// ---------------------------------------------------------------------------

void hw_begin();
void hw_poll(uint32_t now);      // call often; the link reads the rig board every RIG_POLL_MS
bool hw_ok();                    // rig board answering (always true in simulation)
bool hw_channel_ok(int ch);      // current sensor of this channel present
bool hw_has_contacts();          // contact (continuity) inputs fitted
float hw_range_a();              // highest current the sensors can measure

void hw_sim_set_spec(const ModelSpec *spec);  // simulation only (no-op on the link)
void hw_new_cycle(int ch);                    // simulation only (no-op on the link)

void hw_relay(int ch, bool on, uint32_t now); // switching ON also restarts peak/average
void hw_all_off(uint32_t now);
bool hw_relay_state(int ch);
float hw_current(int ch, uint32_t now);       // live coil current, A
bool hw_contact_closed(int ch, uint32_t now);

// Peak and average measured by the rig board since the relay went ON or the
// last hw_stats_reset(). Negative = not available (simulation, or the reset
// is not confirmed yet); the sequencer then uses its own readings.
void hw_stats_reset(int ch);
float hw_peak(int ch);
float hw_avg(int ch);
