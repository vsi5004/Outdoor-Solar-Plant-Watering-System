#include "unity.h"
#include "watering/watering_fsm.hpp"
#include "watering/watering_request.hpp"
#include "mock_zone_manager.hpp"
#include "mock_pump_actuator.hpp"
#include "mock_flow_meter.hpp"
#include "mock_float_sensor.hpp"
#include "mock_renogy_monitor.hpp"
#include "config.hpp"

// ── Fixture ───────────────────────────────────────────────────────────────────

struct Fixture {
    MockZoneManager   zones;
    MockPumpActuator  pump;
    MockFlowMeter     flow;
    MockFloatSensor   tank;
    MockRenogyMonitor renogy;
    WateringFsm       fsm{zones, pump, flow, tank, renogy};
    uint32_t          now = 0;

    static WateringRequest req(ZoneId z = ZoneId::Zone1, uint32_t sec = 60)
    {
        return {z, sec, WaterSource::HaManual};
    }

    bool sendRequest(ZoneId z = ZoneId::Zone1, uint32_t sec = 60)
    {
        return fsm.request(req(z, sec), now);
    }

    void advanceMs(uint32_t ms)
    {
        now += ms;
        fsm.tick(now);
    }

    void simulatePrime()
    {
        flow.pulses_ = static_cast<int32_t>(config::pump::PRIME_PULSE_COUNT);
        fsm.tick(now);
    }
};

// ── Idle state ────────────────────────────────────────────────────────────────

static void test_fsm_starts_idle(void)
{
    Fixture f;
    TEST_ASSERT_EQUAL(ZoneStatus::Idle, f.fsm.getZoneStatus(ZoneId::Zone1));
    TEST_ASSERT_EQUAL(FaultCode::None,  f.fsm.getLastFault());
}

static void test_tick_in_idle_is_safe(void)
{
    Fixture f;
    f.fsm.tick(0);
    TEST_ASSERT_EQUAL(ZoneStatus::Idle, f.fsm.getZoneStatus(ZoneId::Zone1));
}

// ── request() precondition checks ────────────────────────────────────────────

static void test_request_rejected_when_duration_zero(void)
{
    Fixture f;
    TEST_ASSERT_FALSE(f.sendRequest(ZoneId::Zone1, 0));
    TEST_ASSERT_EQUAL(FaultCode::InvalidRequest, f.fsm.getLastFault());
}

static void test_request_rejected_when_battery_low(void)
{
    Fixture f;
    f.renogy.data_.batterySoc = config::safety::MIN_BATTERY_SOC_PCT;
    TEST_ASSERT_FALSE(f.sendRequest());
    TEST_ASSERT_EQUAL(FaultCode::LowBattery, f.fsm.getLastFault());
}

static void test_request_rejected_when_tank_low(void)
{
    Fixture f;
    f.tank.percent_ = config::safety::MIN_WATER_LEVEL_PCT;
    TEST_ASSERT_FALSE(f.sendRequest());
    TEST_ASSERT_EQUAL(FaultCode::LowWater, f.fsm.getLastFault());
}

static void test_load_enable_failure_causes_fault(void)
{
    Fixture f;
    f.renogy.setLoadResult_ = false;
    TEST_ASSERT_FALSE(f.sendRequest());
    TEST_ASSERT_EQUAL(FaultCode::LoadEnableFailed, f.fsm.getLastFault());
}

static void test_load_enable_failure_does_not_start_pump(void)
{
    Fixture f;
    f.renogy.setLoadResult_ = false;
    f.sendRequest();
    TEST_ASSERT_EQUAL(0, f.pump.lastSpeed_);
    TEST_ASSERT_FALSE(f.zones.isOpen(ZoneId::Zone1));
}

// ── request() happy path ──────────────────────────────────────────────────────

static void test_valid_request_returns_true(void)
{
    Fixture f;
    TEST_ASSERT_TRUE(f.sendRequest());
}

static void test_valid_request_enables_renogy_load(void)
{
    Fixture f;
    f.sendRequest();
    TEST_ASSERT_TRUE(f.renogy.lastLoadState_);
    TEST_ASSERT_EQUAL(1, f.renogy.setLoadCallCount_);
}

static void test_valid_request_opens_zone(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone3);
    TEST_ASSERT_TRUE(f.zones.isOpen(ZoneId::Zone3));
}

static void test_valid_request_starts_pump(void)
{
    Fixture f;
    f.sendRequest();
    TEST_ASSERT_EQUAL(100, f.pump.lastSpeed_);
}

static void test_valid_request_resets_flow_meter(void)
{
    Fixture f;
    f.flow.pulses_ = 99;
    f.sendRequest();
    TEST_ASSERT_EQUAL(0,  f.flow.pulses_);
    TEST_ASSERT_EQUAL(1,  f.flow.resetCalls_);
}

static void test_valid_request_zone_status_is_priming(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone2);
    TEST_ASSERT_EQUAL(ZoneStatus::Priming, f.fsm.getZoneStatus(ZoneId::Zone2));
    TEST_ASSERT_EQUAL(ZoneStatus::Idle,    f.fsm.getZoneStatus(ZoneId::Zone1));
}

static void test_request_ignored_when_already_active(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone1);
    TEST_ASSERT_FALSE(f.sendRequest(ZoneId::Zone2));
    TEST_ASSERT_EQUAL(ZoneStatus::Priming, f.fsm.getZoneStatus(ZoneId::Zone1));
    TEST_ASSERT_EQUAL(ZoneStatus::Idle,    f.fsm.getZoneStatus(ZoneId::Zone2));
}

// ── Priming → Watering ────────────────────────────────────────────────────────

static void test_prime_detected_transitions_to_running(void)
{
    Fixture f;
    f.sendRequest();
    f.simulatePrime();
    TEST_ASSERT_EQUAL(ZoneStatus::Running, f.fsm.getZoneStatus(ZoneId::Zone1));
}

static void test_prime_timeout_causes_fault(void)
{
    Fixture f;
    f.sendRequest();
    f.advanceMs(config::pump::PRIME_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(FaultCode::PrimeTimeout, f.fsm.getLastFault());
    TEST_ASSERT_EQUAL(ZoneStatus::Fault, f.fsm.getZoneStatus(ZoneId::Zone1));
}

static void test_prime_timeout_stops_pump_and_closes_zones(void)
{
    Fixture f;
    f.sendRequest();
    f.advanceMs(config::pump::PRIME_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(0,     f.pump.lastSpeed_);
    TEST_ASSERT_FALSE(f.zones.isOpen(ZoneId::Zone1));
}

// ── Watering normal completion ────────────────────────────────────────────────

static void test_watering_stops_after_duration(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone1, 10);
    f.simulatePrime();
    f.advanceMs(10000u);
    TEST_ASSERT_EQUAL(ZoneStatus::Idle, f.fsm.getZoneStatus(ZoneId::Zone1));
    TEST_ASSERT_EQUAL(FaultCode::None,  f.fsm.getLastFault());
}

static void test_watering_closes_zone_and_stops_pump_on_completion(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone2, 5);
    f.simulatePrime();
    f.advanceMs(5000u);
    TEST_ASSERT_EQUAL(0,     f.pump.lastSpeed_);
    TEST_ASSERT_FALSE(f.zones.isOpen(ZoneId::Zone2));
}

static void test_watering_records_delivered_ml(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone1, 5);
    f.simulatePrime();
    f.flow.pulses_ = 100;
    f.advanceMs(5000u);
    const float expected = 100.0f * config::flow::ML_PER_PULSE;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, expected,
                             static_cast<float>(f.fsm.getDeliveredMl()));
}

// ── Watering fault paths ──────────────────────────────────────────────────────

static void test_dry_run_causes_fault(void)
{
    Fixture f;
    f.sendRequest();
    f.simulatePrime();
    f.pump.currentMa_ = config::pump::DRY_RUN_MA - 1.0f;
    f.fsm.tick(f.now);
    TEST_ASSERT_EQUAL(FaultCode::DryRun, f.fsm.getLastFault());
}

static void test_dry_run_stops_pump_and_closes_zones(void)
{
    Fixture f;
    f.sendRequest();
    f.simulatePrime();
    f.pump.currentMa_ = config::pump::DRY_RUN_MA - 1.0f;
    f.fsm.tick(f.now);
    TEST_ASSERT_EQUAL(0,     f.pump.lastSpeed_);
    TEST_ASSERT_FALSE(f.zones.isOpen(ZoneId::Zone1));
}

static void test_max_duration_causes_fault(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone1, 9999);
    f.simulatePrime();
    f.advanceMs(config::pump::MAX_DISPENSE_MS);
    TEST_ASSERT_EQUAL(FaultCode::MaxDuration, f.fsm.getLastFault());
}

static void test_battery_drop_during_watering_causes_fault(void)
{
    Fixture f;
    f.sendRequest();
    f.simulatePrime();
    f.renogy.data_.batterySoc = config::safety::MIN_BATTERY_SOC_PCT;
    f.fsm.tick(f.now);
    TEST_ASSERT_EQUAL(FaultCode::LowBattery, f.fsm.getLastFault());
}

// ── cancel() ─────────────────────────────────────────────────────────────────

static void test_cancel_during_priming_returns_to_idle(void)
{
    Fixture f;
    f.sendRequest();
    f.fsm.cancel();
    TEST_ASSERT_EQUAL(ZoneStatus::Idle, f.fsm.getZoneStatus(ZoneId::Zone1));
    TEST_ASSERT_EQUAL(FaultCode::None,  f.fsm.getLastFault());
}

static void test_cancel_during_watering_returns_to_idle(void)
{
    Fixture f;
    f.sendRequest();
    f.simulatePrime();
    f.fsm.cancel();
    TEST_ASSERT_EQUAL(ZoneStatus::Idle, f.fsm.getZoneStatus(ZoneId::Zone1));
    TEST_ASSERT_EQUAL(FaultCode::None,  f.fsm.getLastFault());
}

static void test_cancel_stops_pump_and_closes_zones(void)
{
    Fixture f;
    f.sendRequest();
    f.simulatePrime();
    f.fsm.cancel();
    TEST_ASSERT_EQUAL(0,     f.pump.lastSpeed_);
    TEST_ASSERT_FALSE(f.zones.isOpen(ZoneId::Zone1));
}

static void test_cancel_disables_renogy_load(void)
{
    Fixture f;
    f.sendRequest();
    f.simulatePrime();
    f.fsm.cancel();
    TEST_ASSERT_FALSE(f.renogy.lastLoadState_);
}

static void test_fault_disables_renogy_load(void)
{
    Fixture f;
    f.sendRequest();
    f.advanceMs(config::pump::PRIME_TIMEOUT_MS); // → PrimeTimeout fault
    TEST_ASSERT_FALSE(f.renogy.lastLoadState_);
}

static void test_watering_completion_disables_renogy_load(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone1, 5);
    f.simulatePrime();
    f.advanceMs(5000u);
    TEST_ASSERT_FALSE(f.renogy.lastLoadState_);
}

static void test_cancel_in_idle_is_safe(void)
{
    Fixture f;
    f.fsm.cancel();
    TEST_ASSERT_EQUAL(ZoneStatus::Idle, f.fsm.getZoneStatus(ZoneId::Zone1));
    TEST_ASSERT_EQUAL(FaultCode::None,  f.fsm.getLastFault());
}

static void test_cancel_in_fault_is_noop(void)
{
    Fixture f;
    // Use a fault that occurs after the zone was accepted so Zone1 is owned.
    f.sendRequest();
    f.advanceMs(config::pump::PRIME_TIMEOUT_MS); // → PrimeTimeout fault
    TEST_ASSERT_EQUAL(FaultCode::PrimeTimeout, f.fsm.getLastFault());
    f.fsm.cancel();
    // cancel() must not clear faults — use clearFault() for that
    TEST_ASSERT_EQUAL(FaultCode::PrimeTimeout, f.fsm.getLastFault());
    TEST_ASSERT_EQUAL(ZoneStatus::Fault,       f.fsm.getZoneStatus(ZoneId::Zone1));
}

static void test_cancel_captures_delivered_ml(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone1, 60);
    f.simulatePrime();
    f.flow.pulses_ = 50;
    f.fsm.cancel();
    const float expected = 50.0f * config::flow::ML_PER_PULSE;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, expected,
                             static_cast<float>(f.fsm.getDeliveredMl()));
}

static void test_new_request_accepted_after_cancel(void)
{
    Fixture f;
    f.sendRequest();
    f.simulatePrime();
    f.fsm.cancel();
    TEST_ASSERT_TRUE(f.sendRequest(ZoneId::Zone2));
}

// ── Fault handling ────────────────────────────────────────────────────────────

static void test_clear_fault_returns_to_idle(void)
{
    Fixture f;
    f.sendRequest(ZoneId::Zone1, 0); // force InvalidRequest fault
    TEST_ASSERT_EQUAL(FaultCode::InvalidRequest, f.fsm.getLastFault());
    f.fsm.clearFault();
    TEST_ASSERT_EQUAL(FaultCode::None,  f.fsm.getLastFault());
    TEST_ASSERT_EQUAL(ZoneStatus::Idle, f.fsm.getZoneStatus(ZoneId::Zone1));
}

static void test_clear_fault_from_idle_is_safe(void)
{
    Fixture f;
    f.fsm.clearFault();
    TEST_ASSERT_EQUAL(FaultCode::None, f.fsm.getLastFault());
}

static void test_new_request_accepted_after_clear_fault(void)
{
    Fixture f;
    f.renogy.data_.batterySoc = config::safety::MIN_BATTERY_SOC_PCT;
    f.sendRequest();
    f.fsm.clearFault();
    f.renogy.data_.batterySoc = 80;
    TEST_ASSERT_TRUE(f.sendRequest());
}

void run_watering_fsm_tests(void)
{
    RUN_TEST(test_fsm_starts_idle);
    RUN_TEST(test_tick_in_idle_is_safe);
    RUN_TEST(test_request_rejected_when_duration_zero);
    RUN_TEST(test_request_rejected_when_battery_low);
    RUN_TEST(test_request_rejected_when_tank_low);
    RUN_TEST(test_load_enable_failure_causes_fault);
    RUN_TEST(test_load_enable_failure_does_not_start_pump);
    RUN_TEST(test_valid_request_returns_true);
    RUN_TEST(test_valid_request_enables_renogy_load);
    RUN_TEST(test_valid_request_opens_zone);
    RUN_TEST(test_valid_request_starts_pump);
    RUN_TEST(test_valid_request_resets_flow_meter);
    RUN_TEST(test_valid_request_zone_status_is_priming);
    RUN_TEST(test_request_ignored_when_already_active);
    RUN_TEST(test_prime_detected_transitions_to_running);
    RUN_TEST(test_prime_timeout_causes_fault);
    RUN_TEST(test_prime_timeout_stops_pump_and_closes_zones);
    RUN_TEST(test_watering_stops_after_duration);
    RUN_TEST(test_watering_closes_zone_and_stops_pump_on_completion);
    RUN_TEST(test_watering_records_delivered_ml);
    RUN_TEST(test_dry_run_causes_fault);
    RUN_TEST(test_dry_run_stops_pump_and_closes_zones);
    RUN_TEST(test_max_duration_causes_fault);
    RUN_TEST(test_battery_drop_during_watering_causes_fault);
    RUN_TEST(test_cancel_during_priming_returns_to_idle);
    RUN_TEST(test_cancel_during_watering_returns_to_idle);
    RUN_TEST(test_cancel_stops_pump_and_closes_zones);
    RUN_TEST(test_cancel_disables_renogy_load);
    RUN_TEST(test_fault_disables_renogy_load);
    RUN_TEST(test_watering_completion_disables_renogy_load);
    RUN_TEST(test_cancel_in_idle_is_safe);
    RUN_TEST(test_cancel_in_fault_is_noop);
    RUN_TEST(test_cancel_captures_delivered_ml);
    RUN_TEST(test_new_request_accepted_after_cancel);
    RUN_TEST(test_clear_fault_returns_to_idle);
    RUN_TEST(test_clear_fault_from_idle_is_safe);
    RUN_TEST(test_new_request_accepted_after_clear_fault);
}
