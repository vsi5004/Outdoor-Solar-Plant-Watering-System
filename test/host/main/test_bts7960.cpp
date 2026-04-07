#include "unity.h"
#include "drivers/bts7960_chip.hpp"
#include "drivers/solenoid_actuator.hpp"
#include "drivers/pump_actuator.hpp"
#include "config.hpp"
#include "mock_adc_channel.hpp"
#include "mock_pwm.hpp"

// ── Bts7960Chip ──────────────────────────────────────────────────────────────

static void test_chip_zero_mv_gives_zero_current(void)
{
    MockAdcChannel adc(0.0f);
    Bts7960Chip chip(adc);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, chip.readCurrentMa());
}

static void test_chip_converts_mv_to_ma(void)
{
    MockAdcChannel adc(1000.0f);
    Bts7960Chip chip(adc);
    // 1000 mV * 8.5 = 8500 mA
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1000.0f * Bts7960Chip::IS_MV_TO_MA, chip.readCurrentMa());
}

static void test_chip_triggers_one_adc_read_per_call(void)
{
    MockAdcChannel adc(500.0f);
    Bts7960Chip chip(adc);
    chip.readCurrentMa();
    chip.readCurrentMa();
    TEST_ASSERT_EQUAL(2, adc.readCount_);
}

// ── SolenoidActuator ─────────────────────────────────────────────────────────
// pullInMs = 0 in all tests to skip vTaskDelay.

static void test_solenoid_starts_closed(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, 0);
    TEST_ASSERT_FALSE(sol.isOpen());
}

static void test_solenoid_open_sequences_100_then_hold(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, 0, 38);
    sol.open();

    TEST_ASSERT_EQUAL(2, pwm.setDutyCallCount_);
    TEST_ASSERT_EQUAL(0, pwm.stopCallCount_);
    TEST_ASSERT_EQUAL(2u, pwm.callLog_.size());
    TEST_ASSERT_EQUAL(MockPwm::Call::Kind::SetDuty, pwm.callLog_[0].kind);
    TEST_ASSERT_EQUAL(100, pwm.callLog_[0].duty);
    TEST_ASSERT_EQUAL(MockPwm::Call::Kind::SetDuty, pwm.callLog_[1].kind);
    TEST_ASSERT_EQUAL(38,  pwm.callLog_[1].duty);
}

static void test_solenoid_open_uses_config_hold_duty_by_default(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, 0);
    sol.open();
    TEST_ASSERT_EQUAL(config::solenoid::HOLD_DUTY_PCT, pwm.callLog_[1].duty);
}

static void test_solenoid_is_open_true_after_open(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, 0);
    sol.open();
    TEST_ASSERT_TRUE(sol.isOpen());
}

static void test_solenoid_close_stops_pwm_and_clears_flag(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, 0);
    sol.open();
    sol.close();
    TEST_ASSERT_FALSE(sol.isOpen());
    TEST_ASSERT_EQUAL(1, pwm.stopCallCount_);
}

static void test_solenoid_close_without_open_calls_stop(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, 0);
    sol.close();
    TEST_ASSERT_FALSE(sol.isOpen());
    TEST_ASSERT_EQUAL(1, pwm.stopCallCount_);
}

// ── PumpActuator ─────────────────────────────────────────────────────────────

static void test_pump_set_speed_passes_duty_to_pwm(void)
{
    MockPwm pwm;
    MockAdcChannel adc;
    Bts7960Chip chip(adc);
    MockPwm sol5pwm;
    SolenoidActuator zone5(sol5pwm, 0);

    PumpActuator pump(pwm, chip, zone5);
    pump.setSpeed(75);
    TEST_ASSERT_EQUAL(75, pwm.lastDuty_);
}

static void test_pump_stop_calls_pwm_stop(void)
{
    MockPwm pwm;
    MockAdcChannel adc;
    Bts7960Chip chip(adc);
    MockPwm sol5pwm;
    SolenoidActuator zone5(sol5pwm, 0);

    PumpActuator pump(pwm, chip, zone5);
    pump.setSpeed(100);
    pump.stop();
    TEST_ASSERT_EQUAL(1, pwm.stopCallCount_);
}

static void test_pump_current_no_correction_when_zone5_closed(void)
{
    MockPwm pwm;
    MockAdcChannel adc(200.0f); // 200 mV * 8.5 = 1700 mA
    Bts7960Chip chip(adc);
    MockPwm sol5pwm;
    SolenoidActuator zone5(sol5pwm, 0);

    PumpActuator pump(pwm, chip, zone5);
    const float expected = 200.0f * Bts7960Chip::IS_MV_TO_MA;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected, pump.readCurrentMa());
}

static void test_pump_current_subtracts_zone5_when_open(void)
{
    MockPwm pwm;
    const float pumpMa  = 1500.0f;
    const float sol5Ma  = config::solenoid::HOLD_CURRENT_MA;
    const float totalMv = (pumpMa + sol5Ma) / Bts7960Chip::IS_MV_TO_MA;

    MockAdcChannel adc(totalMv);
    Bts7960Chip chip(adc);
    MockPwm sol5pwm;
    SolenoidActuator zone5(sol5pwm, 0);
    zone5.open();

    PumpActuator pump(pwm, chip, zone5);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, pumpMa, pump.readCurrentMa());
}

static void test_pump_current_no_correction_after_zone5_closes(void)
{
    MockPwm pwm;
    MockAdcChannel adc(400.0f);
    Bts7960Chip chip(adc);
    MockPwm sol5pwm;
    SolenoidActuator zone5(sol5pwm, 0);

    zone5.open();
    zone5.close();

    PumpActuator pump(pwm, chip, zone5);
    const float expected = 400.0f * Bts7960Chip::IS_MV_TO_MA;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected, pump.readCurrentMa());
}

void run_bts7960_tests(void)
{
    RUN_TEST(test_chip_zero_mv_gives_zero_current);
    RUN_TEST(test_chip_converts_mv_to_ma);
    RUN_TEST(test_chip_triggers_one_adc_read_per_call);
    RUN_TEST(test_solenoid_starts_closed);
    RUN_TEST(test_solenoid_open_sequences_100_then_hold);
    RUN_TEST(test_solenoid_open_uses_config_hold_duty_by_default);
    RUN_TEST(test_solenoid_is_open_true_after_open);
    RUN_TEST(test_solenoid_close_stops_pwm_and_clears_flag);
    RUN_TEST(test_solenoid_close_without_open_calls_stop);
    RUN_TEST(test_pump_set_speed_passes_duty_to_pwm);
    RUN_TEST(test_pump_stop_calls_pwm_stop);
    RUN_TEST(test_pump_current_no_correction_when_zone5_closed);
    RUN_TEST(test_pump_current_subtracts_zone5_when_open);
    RUN_TEST(test_pump_current_no_correction_after_zone5_closes);
}
