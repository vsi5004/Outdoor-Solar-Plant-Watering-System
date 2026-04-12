#pragma once
#include <cstdint>

// Fault codes reported to Home Assistant via the Zigbee fault endpoint (EP 41).
// Values are stable — do not reorder; HA automations may key off the integer.
enum class FaultCode : uint8_t {
    None           = 0,
    LowBattery     = 1,  // Battery SOC below MIN_BATTERY_SOC_PCT
    LowWater       = 2,  // Reservoir below MIN_WATER_LEVEL_PCT
    PrimeTimeout   = 3,  // No flow pulses within PUMP_PRIME_TIMEOUT_MS
    DryRun         = 4,  // Pump IS current below DRY_RUN_MA while dispensing
    MaxDuration       = 5,  // Watering exceeded MAX_DISPENSE_MS hard cap
    InvalidRequest    = 6,  // duration_sec == 0 or zone out of range
    LoadEnableFailed  = 7,  // Renogy setLoad(true) returned false
};
