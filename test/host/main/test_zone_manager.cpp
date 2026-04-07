#include "unity.h"
#include "watering/zone_manager.hpp"
#include "drivers/solenoid_actuator.hpp"
#include "mock_pwm.hpp"
#include <array>

// ── Fixtures ──────────────────────────────────────────────────────────────────

// Build a ZoneManager backed by 5 mock solenoids (pullInMs=0 to skip delay).
struct ZmFixture {
    std::array<MockPwm, ZONE_COUNT>            pwms;
    std::array<SolenoidActuator, ZONE_COUNT>   sols = {
        SolenoidActuator{pwms[0], 0},
        SolenoidActuator{pwms[1], 0},
        SolenoidActuator{pwms[2], 0},
        SolenoidActuator{pwms[3], 0},
        SolenoidActuator{pwms[4], 0},
    };
    ZoneManager mgr{
        {&sols[0], &sols[1], &sols[2], &sols[3], &sols[4]}
    };
};

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_all_zones_closed_initially(void)
{
    ZmFixture f;
    for (uint8_t i = ZONE_ID_MIN; i <= ZONE_ID_MAX; i++) {
        TEST_ASSERT_FALSE(f.mgr.isOpen(static_cast<ZoneId>(i)));
    }
}

static void test_open_opens_correct_solenoid(void)
{
    ZmFixture f;
    f.mgr.open(ZoneId::Zone3);
    TEST_ASSERT_TRUE(f.sols[2].isOpen()); // Zone3 → index 2
}

static void test_open_does_not_affect_other_zones(void)
{
    ZmFixture f;
    f.mgr.open(ZoneId::Zone2);
    TEST_ASSERT_FALSE(f.sols[0].isOpen()); // Zone1
    TEST_ASSERT_FALSE(f.sols[2].isOpen()); // Zone3
    TEST_ASSERT_FALSE(f.sols[3].isOpen()); // Zone4
    TEST_ASSERT_FALSE(f.sols[4].isOpen()); // Zone5
}

static void test_is_open_returns_true_after_open(void)
{
    ZmFixture f;
    f.mgr.open(ZoneId::Zone1);
    TEST_ASSERT_TRUE(f.mgr.isOpen(ZoneId::Zone1));
}

static void test_close_closes_correct_solenoid(void)
{
    ZmFixture f;
    f.mgr.open(ZoneId::Zone4);
    f.mgr.close(ZoneId::Zone4);
    TEST_ASSERT_FALSE(f.sols[3].isOpen());
}

static void test_is_open_returns_false_after_close(void)
{
    ZmFixture f;
    f.mgr.open(ZoneId::Zone5);
    f.mgr.close(ZoneId::Zone5);
    TEST_ASSERT_FALSE(f.mgr.isOpen(ZoneId::Zone5));
}

static void test_close_all_closes_all_open_zones(void)
{
    ZmFixture f;
    f.mgr.open(ZoneId::Zone1);
    f.mgr.open(ZoneId::Zone3);
    f.mgr.open(ZoneId::Zone5);
    f.mgr.closeAll();
    for (uint8_t i = ZONE_ID_MIN; i <= ZONE_ID_MAX; i++) {
        TEST_ASSERT_FALSE(f.mgr.isOpen(static_cast<ZoneId>(i)));
    }
}

static void test_close_all_on_all_closed_is_safe(void)
{
    ZmFixture f;
    f.mgr.closeAll(); // must not crash or assert
    for (uint8_t i = ZONE_ID_MIN; i <= ZONE_ID_MAX; i++) {
        TEST_ASSERT_FALSE(f.mgr.isOpen(static_cast<ZoneId>(i)));
    }
}

static void test_open_already_open_zone_is_idempotent(void)
{
    ZmFixture f;
    f.mgr.open(ZoneId::Zone2);
    f.mgr.open(ZoneId::Zone2); // second open must not crash
    TEST_ASSERT_TRUE(f.mgr.isOpen(ZoneId::Zone2));
}

static void test_close_already_closed_zone_is_safe(void)
{
    ZmFixture f;
    f.mgr.close(ZoneId::Zone1); // must not crash
    TEST_ASSERT_FALSE(f.mgr.isOpen(ZoneId::Zone1));
}

void run_zone_manager_tests(void)
{
    RUN_TEST(test_all_zones_closed_initially);
    RUN_TEST(test_open_opens_correct_solenoid);
    RUN_TEST(test_open_does_not_affect_other_zones);
    RUN_TEST(test_is_open_returns_true_after_open);
    RUN_TEST(test_close_closes_correct_solenoid);
    RUN_TEST(test_is_open_returns_false_after_close);
    RUN_TEST(test_close_all_closes_all_open_zones);
    RUN_TEST(test_close_all_on_all_closed_is_safe);
    RUN_TEST(test_open_already_open_zone_is_idempotent);
    RUN_TEST(test_close_already_closed_zone_is_safe);
}
