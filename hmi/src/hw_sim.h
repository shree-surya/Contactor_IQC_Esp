#pragma once
#include <stdint.h>
#include "app_data.h"

// Simulated relays, INA219s and contact inputs. Produces realistic in-rush /
// hold current curves with noise and an occasional random fault so the
// PASS and FAIL paths of the UI can be exercised without hardware.
void hw_sim_set_spec(const ModelSpec *spec);
void hw_new_cycle(int ch);  // re-rolls this channel's unit-to-unit variation and faults
void hw_relay(int ch, bool on, uint32_t now);
void hw_all_off(uint32_t now);
bool hw_relay_state(int ch);
float hw_current(int ch, uint32_t now);
bool hw_contact_closed(int ch, uint32_t now);
