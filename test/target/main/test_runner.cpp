#include "unity.h"

// Pull in TEST_CASE registrations from each test translation unit.
extern void register_float_sensor_tests();

// ---------------------------------------------------------------------------
// Phase 0 placeholder — proves the target test pipeline builds and flashes.
// Delete this test and add real #includes as hardware drivers are implemented.
// ---------------------------------------------------------------------------

TEST_CASE("placeholder: build and flash pipeline is working", "[placeholder]")
{
    TEST_ASSERT_EQUAL(1, 1);
}

extern "C" void app_main(void)
{
    register_float_sensor_tests();

    // Interactive menu on device — type 'a' + Enter to run all tests,
    // or a test tag to run a subset (e.g. "[float_sensor]").
    unity_run_menu();
}
