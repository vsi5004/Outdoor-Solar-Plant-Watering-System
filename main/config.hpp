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

namespace config
{

    // ── GPIO pin numbers ─────────────────────────────────────────────────────────
    namespace pins
    {

        // UART1 → RS232 adapter → Renogy Wanderer
        constexpr int RENOGY_TX = 4;   // GPIO4  LP_UART_TXD
        constexpr int RENOGY_RX = 5;   // GPIO5  LP_UART_RXD
        constexpr int RENOGY_UART = 1; // UART_NUM_1

        // PCNT hardware pulse counter — flow meter square-wave output
        // GPIO15 used (external JTAG MTDI — safe as GPIO when no JTAG probe attached).
        constexpr int FLOW_METER = 15; // GPIO15

        // ADC — float (water level) sensor
        constexpr int FLOAT_SENSOR = 0; // GPIO0  ADC1_CH0

        // Master enable — all six EN pins across all three BTS7960 boards
        // Drive HIGH to enable, LOW for emergency stop of all outputs.
        constexpr int DRV_MASTER_EN = 21; // GPIO21

        // BTS7960 board #1 — left half: pump (LPWM hardwired GND) | right half: solenoid 5
        constexpr int PUMP_RPWM = 7;  // GPIO7   LEDC_CHANNEL_0
        constexpr int SOL5_RPWM = 11; // GPIO11  LEDC_CHANNEL_1

        // BTS7960 board #2 — left half: solenoid 1 | right half: solenoid 2
        constexpr int SOL1_LPWM = 6;  // GPIO6  LEDC_CHANNEL_2
        constexpr int SOL2_RPWM = 19; // GPIO19  LEDC_CHANNEL_3

        // BTS7960 board #3 — left half: solenoid 3 | right half: solenoid 4
        constexpr int SOL3_LPWM = 20; // GPIO20  LEDC_CHANNEL_4
        constexpr int SOL4_RPWM = 18; // GPIO18  LEDC_CHANNEL_5

    } // namespace pins

    // ── LEDC channel assignments (one channel per active PWM output) ─────────────
    namespace ledc
    {
        constexpr int PUMP_CH = 0; // LEDC_CHANNEL_0 — pump RPWM
        constexpr int SOL5_CH = 1; // LEDC_CHANNEL_1 — solenoid 5 RPWM
        constexpr int SOL1_CH = 2; // LEDC_CHANNEL_2 — solenoid 1 LPWM
        constexpr int SOL2_CH = 3; // LEDC_CHANNEL_3 — solenoid 2 RPWM
        constexpr int SOL3_CH = 4; // LEDC_CHANNEL_4 — solenoid 3 LPWM
        constexpr int SOL4_CH = 5; // LEDC_CHANNEL_5 — solenoid 4 RPWM

        constexpr uint32_t FREQUENCY_HZ = 22'000; // above audible range; coil whine inaudible
        constexpr uint32_t RESOLUTION_BITS = 10;  // duty range: 0–1023
    } // namespace ledc

    // ── ADC channel assignments (ADC1 only — oneshot API) ───────────────────────
    namespace adc
    {
        constexpr int FLOAT_SENSOR_CH = 0; // ADC1_CH0 — float sensor

        // Number of conversions averaged per readMillivolts() call.
        // Higher values reduce noise; each extra sample adds ~100 µs.
        constexpr uint8_t OVERSAMPLE_COUNT = 5;
    } // namespace adc

    // ── Renogy Wanderer Modbus RTU ────────────────────────────────────────────────
    namespace renogy
    {
        constexpr uint32_t BAUD_RATE = 9'600;
        constexpr uint8_t MODBUS_ADDR = 0x01;
        constexpr uint32_t POLL_INTERVAL_MS = 30'000;
        constexpr uint32_t RESPONSE_TIMEOUT_MS = 500;
        // Time between Renogy acknowledging setLoad(true) and energizing the
        // first downstream actuator. If the controller's physical load output
        // rises later than the Modbus acknowledgement, include that extra lag
        // in this value. This also accounts for the time it takes to charge the
        // motor driver's bulk capacitor before it can provide full current to the solenoid.
        constexpr uint32_t LOAD_ENABLE_SETTLE_MS = 4'500;
        // Data older than this triggers FaultCode::StaleData. 3× poll interval
        // gives two missed polls before a watering cycle is blocked.
        constexpr uint32_t STALE_THRESHOLD_MS = 3u * POLL_INTERVAL_MS; // 90 s
    } // namespace renogy

    // ── Solenoid PWM behavior ─────────────────────────────────────────────────────
    namespace solenoid
    {
        // Pull-in phase: full burst to seat the plunger.
        constexpr uint8_t PULL_IN_DUTY_PCT = 85; // empirically determined
        constexpr uint32_t PULL_IN_MS = 100;     // empirically determined

        // Hold duty — enough to keep the plunger seated, low enough to limit heat.
        constexpr uint8_t HOLD_DUTY_PCT = 25; // empirically determined
    } // namespace solenoid

    // ── Pump ──────────────────────────────────────────────────────────────────────
    namespace pump
    {
        // Duty % applied during dispensing — empirically determined.
        constexpr uint8_t DUTY_PCT = 70;

        // Maximum time to wait for flow pulses before declaring a prime fault.
        constexpr uint32_t PRIME_TIMEOUT_MS = 15'000;

        // Minimum pulse count before the pump is considered primed.
        constexpr uint32_t PRIME_PULSE_COUNT = 5;

        // Default and user-configurable per-zone watering durations.
        constexpr uint32_t DEFAULT_WATERING_DURATION_SEC = 15;
        constexpr uint32_t MIN_WATERING_DURATION_SEC = 1;
        constexpr uint32_t MAX_WATERING_DURATION_SEC = 30u * 60u;

        // Hard cap on watering duration regardless of HA command.
        constexpr uint32_t MAX_DISPENSE_MS = 30u * 60u * 1'000u; // 30 minutes
    } // namespace pump

    // Watering output sequencing.
    namespace watering_sequence
    {
        // Graceful shutdown mirrors startup in reverse:
        // pump off -> wait -> solenoid close -> wait -> Renogy load off.
        constexpr uint32_t PUMP_STOP_TO_SOLENOID_CLOSE_MS = solenoid::PULL_IN_MS;
        constexpr uint32_t SOLENOID_CLOSE_TO_LOAD_DISABLE_MS = renogy::LOAD_ENABLE_SETTLE_MS;
    } // namespace watering_sequence

    // ── Zigbee device identity (read by Z2M during interview) ────────────────────
    namespace zigbee
    {
        constexpr char MANUFACTURER_NAME[] = "Ivanbuilds";
        // Treat the model identifier as part of the Zigbee2MQTT contract.
        // If the endpoint shape changes incompatibly, bump this string and
        // ship a matching converter update instead of reusing stale metadata.
        constexpr char MODEL_IDENTIFIER[] = "solar-plant-waterer";

        // Development recovery switch: set true, flash once, let the device
        // boot and erase Zigbee network/reporting state, then set false again.
        // This clears only the Zigbee stack storage partition, not the whole MCU.
        constexpr bool ERASE_NVRAM_ON_BOOT = false;

        // Give Zigbee2MQTT time to finish its interview before this custom
        // device starts sending unsolicited attribute reports.
        constexpr uint32_t REPORT_DELAY_AFTER_JOIN_MS = 60'000;
    } // namespace zigbee

    // ── Safety thresholds ─────────────────────────────────────────────────────────
    namespace safety
    {
        // Watering is blocked at or below these levels.
        constexpr uint8_t MIN_BATTERY_SOC_PCT = 15;
        constexpr uint8_t MIN_WATER_LEVEL_PCT = 5;
    } // namespace safety

    // ── Flow meter calibration ───────────────────────────────────────────────────
    namespace flow
    {
        // mL per rising-edge pulse on the flow meter output.
        // Derived from datasheet: F = 23.6 × Q (Hz, L/min)
        //   mL/pulse = 1000 / (60 × 23.6) ≈ 0.706 mL/pulse (flow-rate independent)
        // Verify empirically: pump a known volume and divide by pulse count.
        constexpr float ML_PER_PULSE = 0.706f;
    } // namespace flow

    // ── Float sensor calibration ─────────────────────────────────────────────────
    namespace sensor
    {
        // ADC millivolt readings at the extremes of the reservoir.
        // The sensor has lower resistance when full, so the voltage divider
        // (sensor as lower leg) gives a higher voltage when empty and lower
        // voltage when full: FLOAT_EMPTY_MV > FLOAT_FULL_MV.
        // CALIBRATE: measure with reservoir completely empty and completely full.
        constexpr float FLOAT_EMPTY_MV = 1390.0f;
        constexpr float FLOAT_FULL_MV = 315.0f;
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

static_assert(config::renogy::LOAD_ENABLE_SETTLE_MS > 0,
              "LOAD_ENABLE_SETTLE_MS must be positive");

static_assert(config::watering_sequence::PUMP_STOP_TO_SOLENOID_CLOSE_MS > 0,
              "PUMP_STOP_TO_SOLENOID_CLOSE_MS must be positive");

static_assert(config::watering_sequence::SOLENOID_CLOSE_TO_LOAD_DISABLE_MS > 0,
              "SOLENOID_CLOSE_TO_LOAD_DISABLE_MS must be positive");

static_assert(config::pump::PRIME_PULSE_COUNT > 0,
              "PRIME_PULSE_COUNT must be positive");

static_assert(config::flow::ML_PER_PULSE > 0.0f,
              "ML_PER_PULSE must be positive");

static_assert(config::sensor::FLOAT_FULL_MV != config::sensor::FLOAT_EMPTY_MV,
              "FLOAT_FULL_MV and FLOAT_EMPTY_MV must differ");

static_assert(config::ledc::RESOLUTION_BITS >= 1 && config::ledc::RESOLUTION_BITS <= 14,
              "LEDC resolution must be 1–14 bits (ESP32-C6 hardware limit)");
