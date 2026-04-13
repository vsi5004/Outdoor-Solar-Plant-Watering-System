#include "unity.h"
#include "drivers/solenoid_actuator.hpp"
#include "drivers/pump_actuator.hpp"
#include "config.hpp"
#include "mock_pwm.hpp"

// ── SolenoidActuator ─────────────────────────────────────────────────────────
// pullInMs = 0 in all tests to skip vTaskDelay.

static void test_solenoid_starts_closed(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, 0);
    TEST_ASSERT_FALSE(sol.isOpen());
}

static void test_solenoid_open_sequences_pull_in_then_hold(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, /*pullInMs=*/0, /*holdDutyPct=*/38, /*pullInDutyPct=*/75);
    sol.open();

    TEST_ASSERT_EQUAL(2, pwm.setDutyCallCount_);
    TEST_ASSERT_EQUAL(0, pwm.stopCallCount_);
    TEST_ASSERT_EQUAL(2u, pwm.callLog_.size());
    TEST_ASSERT_EQUAL(MockPwm::Call::Kind::SetDuty, pwm.callLog_[0].kind);
    TEST_ASSERT_EQUAL(75, pwm.callLog_[0].duty);
    TEST_ASSERT_EQUAL(MockPwm::Call::Kind::SetDuty, pwm.callLog_[1].kind);
    TEST_ASSERT_EQUAL(38, pwm.callLog_[1].duty);
}

static void test_solenoid_open_uses_config_duties_by_default(void)
{
    MockPwm pwm;
    SolenoidActuator sol(pwm, 0);
    sol.open();
    TEST_ASSERT_EQUAL(config::solenoid::PULL_IN_DUTY_PCT, pwm.callLog_[0].duty);
    TEST_ASSERT_EQUAL(config::solenoid::HOLD_DUTY_PCT,    pwm.callLog_[1].duty);
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
    PumpActuator pump(pwm);
    pump.setSpeed(75);
    TEST_ASSERT_EQUAL(75, pwm.lastDuty_);
}

static void test_pump_stop_calls_pwm_stop(void)
{
    MockPwm pwm;
    PumpActuator pump(pwm);
    pump.setSpeed(100);
    pump.stop();
    TEST_ASSERT_EQUAL(1, pwm.stopCallCount_);
}

void run_bts7960_tests(void)
{
    RUN_TEST(test_solenoid_starts_closed);
    RUN_TEST(test_solenoid_open_sequences_pull_in_then_hold);
    RUN_TEST(test_solenoid_open_uses_config_duties_by_default);
    RUN_TEST(test_solenoid_is_open_true_after_open);
    RUN_TEST(test_solenoid_close_stops_pwm_and_clears_flag);
    RUN_TEST(test_solenoid_close_without_open_calls_stop);
    RUN_TEST(test_pump_set_speed_passes_duty_to_pwm);
    RUN_TEST(test_pump_stop_calls_pwm_stop);
}
