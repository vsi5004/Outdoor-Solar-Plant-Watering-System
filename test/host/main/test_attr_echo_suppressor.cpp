#include "unity.h"
#include "zb/attr_echo_suppressor.hpp"

namespace {

constexpr AttrEchoSuppressor::Key kZone1OnOff = {
    10u,
    0x0006u,
    0x0000u,
};

constexpr AttrEchoSuppressor::Key kZone1Duration = {
    10u,
    0x000du,
    0x0055u,
};

} // namespace

static void test_suppressor_consumes_matching_local_write_once(void)
{
    AttrEchoSuppressor suppressor;
    const uint8_t on = 1u;

    TEST_ASSERT_TRUE(suppressor.remember(kZone1OnOff, on));
    TEST_ASSERT_TRUE(suppressor.consume(kZone1OnOff, on));
    TEST_ASSERT_FALSE(suppressor.consume(kZone1OnOff, on));
}

static void test_suppressor_rejects_mismatched_value(void)
{
    AttrEchoSuppressor suppressor;
    const uint8_t on  = 1u;
    const uint8_t off = 0u;

    TEST_ASSERT_TRUE(suppressor.remember(kZone1OnOff, on));
    TEST_ASSERT_FALSE(suppressor.consume(kZone1OnOff, off));
    TEST_ASSERT_TRUE(suppressor.consume(kZone1OnOff, on));
}

static void test_suppressor_tracks_cluster_and_attribute_identity(void)
{
    AttrEchoSuppressor suppressor;
    const float durationSec = 15.0f;

    TEST_ASSERT_TRUE(suppressor.remember(kZone1Duration, durationSec));
    TEST_ASSERT_FALSE(suppressor.consume(kZone1OnOff, static_cast<uint8_t>(1u)));
    TEST_ASSERT_TRUE(suppressor.consume(kZone1Duration, durationSec));
}

static void test_suppressor_clear_discards_pending_entry(void)
{
    AttrEchoSuppressor suppressor;
    const uint8_t off = 0u;

    TEST_ASSERT_TRUE(suppressor.remember(kZone1OnOff, off));
    suppressor.clear(kZone1OnOff);
    TEST_ASSERT_FALSE(suppressor.consume(kZone1OnOff, off));
}

static void test_suppressor_overwrites_existing_entry_for_same_key(void)
{
    AttrEchoSuppressor suppressor;
    const uint8_t on  = 1u;
    const uint8_t off = 0u;

    TEST_ASSERT_TRUE(suppressor.remember(kZone1OnOff, on));
    TEST_ASSERT_TRUE(suppressor.remember(kZone1OnOff, off));
    TEST_ASSERT_FALSE(suppressor.consume(kZone1OnOff, on));
    TEST_ASSERT_TRUE(suppressor.consume(kZone1OnOff, off));
}

void run_attr_echo_suppressor_tests()
{
    RUN_TEST(test_suppressor_consumes_matching_local_write_once);
    RUN_TEST(test_suppressor_rejects_mismatched_value);
    RUN_TEST(test_suppressor_tracks_cluster_and_attribute_identity);
    RUN_TEST(test_suppressor_clear_discards_pending_entry);
    RUN_TEST(test_suppressor_overwrites_existing_entry_for_same_key);
}
