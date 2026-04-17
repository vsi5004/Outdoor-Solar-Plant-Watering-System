#include "unity.h"

#include "watering/water_usage_tracker.hpp"

static void test_water_usage_starts_at_zero(void)
{
    WaterUsageTracker tracker;

    TEST_ASSERT_EQUAL_UINT64(0, tracker.getTotalMl(ZoneId::Zone1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tracker.getTotalLiters(ZoneId::Zone1));
}

static void test_water_usage_accumulates_per_zone(void)
{
    WaterUsageTracker tracker;

    TEST_ASSERT_TRUE(tracker.addDelivery(ZoneId::Zone1, 125));
    TEST_ASSERT_TRUE(tracker.addDelivery(ZoneId::Zone1, 875));
    TEST_ASSERT_TRUE(tracker.addDelivery(ZoneId::Zone2, 250));

    TEST_ASSERT_EQUAL_UINT64(1000, tracker.getTotalMl(ZoneId::Zone1));
    TEST_ASSERT_EQUAL_UINT64(250, tracker.getTotalMl(ZoneId::Zone2));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, tracker.getTotalLiters(ZoneId::Zone1));
}

static void test_water_usage_ignores_zero_delivery(void)
{
    WaterUsageTracker tracker;

    TEST_ASSERT_FALSE(tracker.addDelivery(ZoneId::Zone1, 0));
    TEST_ASSERT_EQUAL_UINT64(0, tracker.getTotalMl(ZoneId::Zone1));
}

static void test_water_usage_restores_persisted_total(void)
{
    WaterUsageTracker tracker;

    tracker.setTotalMl(ZoneId::Zone4, 12345);

    TEST_ASSERT_EQUAL_UINT64(12345, tracker.getTotalMl(ZoneId::Zone4));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.345f, tracker.getTotalLiters(ZoneId::Zone4));
}

void run_water_usage_tracker_tests(void)
{
    RUN_TEST(test_water_usage_starts_at_zero);
    RUN_TEST(test_water_usage_accumulates_per_zone);
    RUN_TEST(test_water_usage_ignores_zero_delivery);
    RUN_TEST(test_water_usage_restores_persisted_total);
}
