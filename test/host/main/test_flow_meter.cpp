#include "unity.h"
#include "drivers/flow_meter.hpp"
#include "config.hpp"
#include "mock_pulse_counter.hpp"

// ── FlowMeter ────────────────────────────────────────────────────────────────

static void test_flow_zero_pulses_gives_zero_ml(void)
{
    MockPulseCounter ctr;
    FlowMeter fm(ctr);
    TEST_ASSERT_EQUAL(0,    fm.getPulses());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, fm.getMilliliters());
}

static void test_flow_get_pulses_reflects_counter(void)
{
    MockPulseCounter ctr;
    FlowMeter fm(ctr);
    ctr.setCount(42);
    TEST_ASSERT_EQUAL(42, fm.getPulses());
}

static void test_flow_get_ml_uses_ml_per_pulse(void)
{
    MockPulseCounter ctr;
    FlowMeter fm(ctr);
    ctr.setCount(10);
    const float expected = 10.0f * config::flow::ML_PER_PULSE;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected, fm.getMilliliters());
}

static void test_flow_get_ml_custom_calibration(void)
{
    MockPulseCounter ctr;
    constexpr float custom = 3.5f;
    FlowMeter fm(ctr, custom);
    ctr.setCount(100);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f * custom, fm.getMilliliters());
}

static void test_flow_reset_delegates_to_counter(void)
{
    MockPulseCounter ctr;
    ctr.setCount(200);
    FlowMeter fm(ctr);

    fm.reset();

    TEST_ASSERT_EQUAL(1, ctr.resetCount_);
    TEST_ASSERT_EQUAL(0, fm.getPulses());
}

static void test_flow_reset_zeroes_volume(void)
{
    MockPulseCounter ctr;
    FlowMeter fm(ctr);
    ctr.setCount(50);
    fm.reset(); // MockPulseCounter::reset() zeroes count_
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, fm.getMilliliters());
}

static void test_flow_accumulates_across_multiple_reads(void)
{
    MockPulseCounter ctr;
    FlowMeter fm(ctr);

    ctr.setCount(5);
    TEST_ASSERT_EQUAL(5, fm.getPulses());

    ctr.setCount(23);
    TEST_ASSERT_EQUAL(23, fm.getPulses());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.0f * config::flow::ML_PER_PULSE,
                             fm.getMilliliters());
}

static void test_flow_prime_detection_threshold(void)
{
    // Verify PUMP_PRIME_PULSE_COUNT can be used as a threshold against getPulses().
    MockPulseCounter ctr;
    FlowMeter fm(ctr);

    ctr.setCount(static_cast<int32_t>(config::pump::PRIME_PULSE_COUNT) - 1);
    TEST_ASSERT_FALSE(fm.getPulses() > static_cast<int32_t>(config::pump::PRIME_PULSE_COUNT));

    ctr.setCount(static_cast<int32_t>(config::pump::PRIME_PULSE_COUNT) + 1);
    TEST_ASSERT_TRUE(fm.getPulses() > static_cast<int32_t>(config::pump::PRIME_PULSE_COUNT));
}

static void test_flow_large_count_does_not_overflow_float(void)
{
    // 32767 is the PCNT hardware high limit from the spec.
    MockPulseCounter ctr;
    FlowMeter fm(ctr);
    ctr.setCount(32767);
    const float ml = fm.getMilliliters();
    TEST_ASSERT_GREATER_THAN(0.0f, ml);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 32767.0f * config::flow::ML_PER_PULSE, ml);
}

void run_flow_meter_tests(void)
{
    RUN_TEST(test_flow_zero_pulses_gives_zero_ml);
    RUN_TEST(test_flow_get_pulses_reflects_counter);
    RUN_TEST(test_flow_get_ml_uses_ml_per_pulse);
    RUN_TEST(test_flow_get_ml_custom_calibration);
    RUN_TEST(test_flow_reset_delegates_to_counter);
    RUN_TEST(test_flow_reset_zeroes_volume);
    RUN_TEST(test_flow_accumulates_across_multiple_reads);
    RUN_TEST(test_flow_prime_detection_threshold);
    RUN_TEST(test_flow_large_count_does_not_overflow_float);
}
