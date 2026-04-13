#include "unity.h"
#include "config.hpp"
#include "watering/zone_id.hpp"
#include "watering/fault_code.hpp"
#include "watering/zone_status.hpp"
#include "watering/watering_request.hpp"

// ── config.hpp ───────────────────────────────────────────────────────────────

static void test_safety_thresholds_in_range(void)
{
    TEST_ASSERT_LESS_THAN(50, config::safety::MIN_BATTERY_SOC_PCT);
    TEST_ASSERT_LESS_THAN(50, config::safety::MIN_WATER_LEVEL_PCT);
    TEST_ASSERT_GREATER_THAN(0, config::safety::MIN_BATTERY_SOC_PCT);
    TEST_ASSERT_GREATER_THAN(0, config::safety::MIN_WATER_LEVEL_PCT);
}

static void test_solenoid_hold_duty_in_pwm_range(void)
{
    TEST_ASSERT_GREATER_THAN(0,    config::solenoid::HOLD_DUTY_PCT);
    TEST_ASSERT_LESS_OR_EQUAL(100, config::solenoid::HOLD_DUTY_PCT);
}

static void test_pump_hard_cap_exceeds_prime_timeout(void)
{
    TEST_ASSERT_GREATER_THAN(config::pump::PRIME_TIMEOUT_MS,
                             config::pump::MAX_DISPENSE_MS);
}

static void test_float_sensor_calibration_range_valid(void)
{
    // Sensor is the lower leg of a voltage divider: lower resistance when full
    // → lower voltage when full. EMPTY_MV > FULL_MV is the correct relationship.
    TEST_ASSERT_GREATER_THAN(config::sensor::FLOAT_FULL_MV,
                             config::sensor::FLOAT_EMPTY_MV);
}

static void test_ml_per_pulse_positive(void)
{
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, config::flow::ML_PER_PULSE);
}

// ── ZoneId ───────────────────────────────────────────────────────────────────

static void test_valid_zone_ids_accepted(void)
{
    TEST_ASSERT_TRUE(isValidZoneId(ZoneId::Zone1));
    TEST_ASSERT_TRUE(isValidZoneId(ZoneId::Zone5));
}

static void test_out_of_range_zone_rejected(void)
{
    TEST_ASSERT_FALSE(isValidZoneId(static_cast<ZoneId>(0)));
    TEST_ASSERT_FALSE(isValidZoneId(static_cast<ZoneId>(6)));
}

static void test_zone_index_mapping(void)
{
    TEST_ASSERT_EQUAL(0, zoneIndex(ZoneId::Zone1));
    TEST_ASSERT_EQUAL(4, zoneIndex(ZoneId::Zone5));
}

static void test_zone_id_underlying_value_matches_zigbee_ep(void)
{
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(ZoneId::Zone1));
    TEST_ASSERT_EQUAL(5, static_cast<uint8_t>(ZoneId::Zone5));
}

// ── Stable enum integer values (HA and Z2M key off these) ───────────────────

static void test_fault_code_values_stable(void)
{
    TEST_ASSERT_EQUAL(0, static_cast<uint8_t>(FaultCode::None));
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(FaultCode::LowBattery));
    TEST_ASSERT_EQUAL(2, static_cast<uint8_t>(FaultCode::LowWater));
    TEST_ASSERT_EQUAL(3, static_cast<uint8_t>(FaultCode::PrimeTimeout));
    TEST_ASSERT_EQUAL(5, static_cast<uint8_t>(FaultCode::MaxDuration));
    TEST_ASSERT_EQUAL(6, static_cast<uint8_t>(FaultCode::InvalidRequest));
    TEST_ASSERT_EQUAL(7, static_cast<uint8_t>(FaultCode::LoadEnableFailed));
}

static void test_zone_status_values_stable(void)
{
    TEST_ASSERT_EQUAL(0, static_cast<uint8_t>(ZoneStatus::Idle));
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(ZoneStatus::Priming));
    TEST_ASSERT_EQUAL(2, static_cast<uint8_t>(ZoneStatus::Running));
    TEST_ASSERT_EQUAL(3, static_cast<uint8_t>(ZoneStatus::Fault));
}

void run_config_tests(void)
{
    RUN_TEST(test_safety_thresholds_in_range);
    RUN_TEST(test_solenoid_hold_duty_in_pwm_range);
    RUN_TEST(test_pump_hard_cap_exceeds_prime_timeout);
    RUN_TEST(test_float_sensor_calibration_range_valid);
    RUN_TEST(test_ml_per_pulse_positive);
    RUN_TEST(test_valid_zone_ids_accepted);
    RUN_TEST(test_out_of_range_zone_rejected);
    RUN_TEST(test_zone_index_mapping);
    RUN_TEST(test_zone_id_underlying_value_matches_zigbee_ep);
    RUN_TEST(test_fault_code_values_stable);
    RUN_TEST(test_zone_status_values_stable);
}
