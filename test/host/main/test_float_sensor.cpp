#include "unity.h"
#include "drivers/float_sensor.hpp"
#include "config.hpp"
#include "mock_adc_channel.hpp"

// Calibration points used across tests.
static constexpr float EMPTY_MV = 400.0f;
static constexpr float FULL_MV  = 2800.0f;
static constexpr float RANGE_MV = FULL_MV - EMPTY_MV; // 2400 mV

// ── FloatSensor ──────────────────────────────────────────────────────────────

static void test_float_at_empty_mv_returns_0_pct(void)
{
    MockAdcChannel adc(EMPTY_MV);
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    TEST_ASSERT_EQUAL(0, sensor.getPercent());
}

static void test_float_at_full_mv_returns_100_pct(void)
{
    MockAdcChannel adc(FULL_MV);
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    TEST_ASSERT_EQUAL(100, sensor.getPercent());
}

static void test_float_at_midpoint_returns_50_pct(void)
{
    MockAdcChannel adc(EMPTY_MV + RANGE_MV * 0.5f);
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    TEST_ASSERT_EQUAL(50, sensor.getPercent());
}

static void test_float_at_quarter_returns_25_pct(void)
{
    MockAdcChannel adc(EMPTY_MV + RANGE_MV * 0.25f);
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    TEST_ASSERT_EQUAL(25, sensor.getPercent());
}

static void test_float_at_three_quarter_returns_75_pct(void)
{
    MockAdcChannel adc(EMPTY_MV + RANGE_MV * 0.75f);
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    TEST_ASSERT_EQUAL(75, sensor.getPercent());
}

static void test_float_below_empty_clamped_to_0(void)
{
    MockAdcChannel adc(EMPTY_MV - 200.0f); // well below empty
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    TEST_ASSERT_EQUAL(0, sensor.getPercent());
}

static void test_float_above_full_clamped_to_100(void)
{
    MockAdcChannel adc(FULL_MV + 500.0f); // well above full
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    TEST_ASSERT_EQUAL(100, sensor.getPercent());
}

static void test_float_zero_mv_clamped_to_0(void)
{
    // Disconnected or shorted sensor reads 0 V — must not underflow.
    MockAdcChannel adc(0.0f);
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    TEST_ASSERT_EQUAL(0, sensor.getPercent());
}

static void test_float_uses_config_defaults_when_not_specified(void)
{
    // Default constructor uses config::sensor::FLOAT_EMPTY_MV / FLOAT_FULL_MV.
    // Verify it compiles and returns 0 when at the empty calibration point.
    MockAdcChannel adc(config::sensor::FLOAT_EMPTY_MV);
    FloatSensor sensor(adc); // no calibration args
    TEST_ASSERT_EQUAL(0, sensor.getPercent());
}

static void test_float_triggers_one_adc_read_per_call(void)
{
    MockAdcChannel adc(EMPTY_MV);
    FloatSensor sensor(adc, EMPTY_MV, FULL_MV);
    sensor.getPercent();
    sensor.getPercent();
    TEST_ASSERT_EQUAL(2, adc.readCount_);
}

static void test_float_min_water_threshold_comparison(void)
{
    // FSM compares getPercent() < MIN_WATER_LEVEL_PCT to gate watering.
    // Verify a reading just below the threshold correctly triggers the gate.
    const uint8_t threshold = config::safety::MIN_WATER_LEVEL_PCT;
    const float   thresholdMv = EMPTY_MV + RANGE_MV * (threshold / 100.0f);

    MockAdcChannel adcLow(thresholdMv - 50.0f); // just below threshold
    FloatSensor sensorLow(adcLow, EMPTY_MV, FULL_MV);
    TEST_ASSERT_TRUE(sensorLow.getPercent() < threshold);

    MockAdcChannel adcHigh(thresholdMv + 50.0f); // just above threshold
    FloatSensor sensorHigh(adcHigh, EMPTY_MV, FULL_MV);
    TEST_ASSERT_FALSE(sensorHigh.getPercent() < threshold);
}

void run_float_sensor_tests(void)
{
    RUN_TEST(test_float_at_empty_mv_returns_0_pct);
    RUN_TEST(test_float_at_full_mv_returns_100_pct);
    RUN_TEST(test_float_at_midpoint_returns_50_pct);
    RUN_TEST(test_float_at_quarter_returns_25_pct);
    RUN_TEST(test_float_at_three_quarter_returns_75_pct);
    RUN_TEST(test_float_below_empty_clamped_to_0);
    RUN_TEST(test_float_above_full_clamped_to_100);
    RUN_TEST(test_float_zero_mv_clamped_to_0);
    RUN_TEST(test_float_uses_config_defaults_when_not_specified);
    RUN_TEST(test_float_triggers_one_adc_read_per_call);
    RUN_TEST(test_float_min_water_threshold_comparison);
}
