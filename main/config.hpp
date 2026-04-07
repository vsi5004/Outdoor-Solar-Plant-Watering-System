#pragma once
#include <cstdint>

// =============================================================================
// Hardware and firmware configuration — single source of truth.
//
// All pin numbers are bare integers so this header is portable to the host
// (linux) test target with no ESP-IDF dependencies. Driver constructors cast
// to gpio_num_t / ledc_channel_t / adc_channel_t as needed.
//
// Calibration placeholders are marked — measure empirically before deployment.
// =============================================================================

namespace config {

// ── GPIO pin numbers ─────────────────────────────────────────────────────────
namespace pins {

    // UART1 → RS232 adapter → Renogy Wanderer
    constexpr int RENOGY_TX   = 4;    // GPIO4  LP_UART_TXD
    constexpr int RENOGY_RX   = 5;    // GPIO5  LP_UART_RXD
    constexpr int RENOGY_UART = 1;    // UART_NUM_1

    // PCNT hardware pulse counter — flow meter square-wave output
    constexpr int FLOW_METER  = 6;    // GPIO6  (ADC1_CH6 — PCNT takes priority)

    // ADC — float (water level) sensor
    constexpr int FLOAT_SENSOR = 0;   // GPIO0  ADC1_CH0

    // Master enable — all six EN pins across all three BTS7960 boards
    // Drive HIGH to enable, LOW for emergency stop of all outputs.
    constexpr int DRV_MASTER_EN = 21; // GPIO21

    // BTS7960 #1 — left half: pump | right half: solenoid 5
    // LPWM is hardwired to GND on the board (one-way valve; no reverse needed).
    constexpr int PUMP_RPWM  = 7;    // GPIO7   LEDC_CHANNEL_0
    constexpr int DRV1_IS    = 1;    // GPIO1   ADC1_CH1 — shared: pump + sol5
    constexpr int SOL5_RPWM  = 10;   // GPIO10  LEDC_CHANNEL_1

    // BTS7960 #2 — left half: solenoid 1 | right half: solenoid 2
    constexpr int SOL1_LPWM  = 11;   // GPIO11  LEDC_CHANNEL_2
    constexpr int SOL2_RPWM  = 18;   // GPIO18  LEDC_CHANNEL_3
    constexpr int DRV2_IS    = 2;    // GPIO2   ADC1_CH2 — shared: sol1 + sol2

    // BTS7960 #3 — left half: solenoid 3 | right half: solenoid 4
    constexpr int SOL3_LPWM  = 19;   // GPIO19  LEDC_CHANNEL_4
    constexpr int SOL4_RPWM  = 20;   // GPIO20  LEDC_CHANNEL_5
    constexpr int DRV3_IS    = 3;    // GPIO3   ADC1_CH3 — shared: sol3 + sol4

} // namespace pins

// ── LEDC channel assignments (one channel per active PWM output) ─────────────
namespace ledc {
    constexpr int PUMP_CH  = 0;  // LEDC_CHANNEL_0 — pump RPWM
    constexpr int SOL5_CH  = 1;  // LEDC_CHANNEL_1 — solenoid 5 RPWM
    constexpr int SOL1_CH  = 2;  // LEDC_CHANNEL_2 — solenoid 1 LPWM
    constexpr int SOL2_CH  = 3;  // LEDC_CHANNEL_3 — solenoid 2 RPWM
    constexpr int SOL3_CH  = 4;  // LEDC_CHANNEL_4 — solenoid 3 LPWM
    constexpr int SOL4_CH  = 5;  // LEDC_CHANNEL_5 — solenoid 4 RPWM

    constexpr uint32_t FREQUENCY_HZ    = 1'000;
    constexpr uint32_t RESOLUTION_BITS = 10;    // duty range: 0–1023
} // namespace ledc

// ── ADC channel assignments (ADC1 only — oneshot API) ───────────────────────
namespace adc {
    constexpr int FLOAT_SENSOR_CH = 0;  // ADC1_CH0 — float sensor
    constexpr int DRV1_IS_CH      = 1;  // ADC1_CH1 — BTS7960 #1 IS pin
    constexpr int DRV2_IS_CH      = 2;  // ADC1_CH2 — BTS7960 #2 IS pin
    constexpr int DRV3_IS_CH      = 3;  // ADC1_CH3 — BTS7960 #3 IS pin
} // namespace adc

// ── Renogy Wanderer Modbus RTU ────────────────────────────────────────────────
namespace renogy {
    constexpr uint32_t BAUD_RATE            = 9'600;
    constexpr uint8_t  MODBUS_ADDR          = 0x01;
    constexpr uint32_t POLL_INTERVAL_MS     = 30'000;
    constexpr uint32_t RESPONSE_TIMEOUT_MS  = 500;
} // namespace renogy

// ── Solenoid PWM behavior ─────────────────────────────────────────────────────
namespace solenoid {
    // Full-duty pull-in phase duration before dropping to hold duty.
    constexpr uint32_t PULL_IN_MS       = 120;

    // Hold duty — enough to keep the solenoid open, low enough to limit heat.
    // Typical range: 30–40%.
    constexpr uint8_t  HOLD_DUTY_PCT    = 38;

    // Expected zone 5 solenoid hold current (mA).
    // Used to correct BTS7960 #1 IS reading when pump and solenoid 5 run
    // simultaneously (they share the same IS pin).
    // CALIBRATE: measure with a clamp meter during zone 5 hold phase.
    constexpr float    HOLD_CURRENT_MA  = 180.0f; // [CALIBRATE]
} // namespace solenoid

// ── Pump ──────────────────────────────────────────────────────────────────────
namespace pump {
    // Maximum time to wait for flow pulses before declaring a prime fault.
    constexpr uint32_t PRIME_TIMEOUT_MS  = 15'000;

    // Minimum pulse count before the pump is considered primed.
    constexpr uint32_t PRIME_PULSE_COUNT = 5;

    // IS current below this level while running = dry-run fault.
    constexpr float    DRY_RUN_MA        = 200.0f;

    // Hard cap on watering duration regardless of HA command.
    constexpr uint32_t MAX_DISPENSE_MS   = 30u * 60u * 1'000u; // 30 minutes
} // namespace pump

// ── Safety thresholds ─────────────────────────────────────────────────────────
namespace safety {
    // Watering is blocked at or below these levels.
    constexpr uint8_t MIN_BATTERY_SOC_PCT = 15;
    constexpr uint8_t MIN_WATER_LEVEL_PCT = 10;
} // namespace safety

// ── Flow meter calibration ───────────────────────────────────────────────────
namespace flow {
    // mL per rising-edge pulse on the flow meter output.
    // CALIBRATE: pump a known volume into a measuring container and divide
    //            by the raw pulse count reported by flow_meter_get_pulses().
    constexpr float ML_PER_PULSE = 2.1f; // [CALIBRATE]
} // namespace flow

// ── Float sensor calibration ─────────────────────────────────────────────────
namespace sensor {
    // ADC millivolt readings at the extremes of the reservoir.
    // CALIBRATE: read float_sensor ADC raw at completely empty and completely
    //            full reservoir; convert to mV using the adc_cali API.
    //            Consider persisting calibrated values to NVS.
    constexpr float FLOAT_EMPTY_MV = 300.0f;   // [CALIBRATE]
    constexpr float FLOAT_FULL_MV  = 2'800.0f; // [CALIBRATE]
} // namespace sensor

} // namespace config

// =============================================================================
// Compile-time sanity checks
// =============================================================================

static_assert(config::safety::MIN_BATTERY_SOC_PCT < 50,
    "MIN_BATTERY_SOC_PCT >= 50 would block most watering — check the value");

static_assert(config::safety::MIN_WATER_LEVEL_PCT < 50,
    "MIN_WATER_LEVEL_PCT >= 50 would block most watering — check the value");

static_assert(config::solenoid::HOLD_DUTY_PCT > 0 && config::solenoid::HOLD_DUTY_PCT <= 100,
    "HOLD_DUTY_PCT must be in range 1–100");

static_assert(config::solenoid::PULL_IN_MS > 0,
    "PULL_IN_MS must be positive");

static_assert(config::pump::PRIME_PULSE_COUNT > 0,
    "PRIME_PULSE_COUNT must be positive");

static_assert(config::flow::ML_PER_PULSE > 0.0f,
    "ML_PER_PULSE must be positive");

static_assert(config::sensor::FLOAT_FULL_MV > config::sensor::FLOAT_EMPTY_MV,
    "FLOAT_FULL_MV must be greater than FLOAT_EMPTY_MV");

static_assert(config::ledc::RESOLUTION_BITS >= 1 && config::ledc::RESOLUTION_BITS <= 14,
    "LEDC resolution must be 1–14 bits (ESP32-C6 hardware limit)");
