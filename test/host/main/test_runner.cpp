#include "unity.h"
#include <cstdlib>

// Forward declarations — each test file exposes one run_xxx_tests() function.
// Add new entries here as test files are added.
void run_placeholder_tests();
void run_config_tests();
void run_mock_tests();
void run_bts7960_tests();
void run_flow_meter_tests();
void run_float_sensor_tests();
void run_renogy_tests();
void run_zone_manager_tests();
void run_watering_fsm_tests();
void run_water_usage_tracker_tests();

extern "C" void app_main(void)
{
    UNITY_BEGIN();

    run_placeholder_tests();
    run_config_tests();
    run_mock_tests();
    run_bts7960_tests();
    run_flow_meter_tests();
    run_float_sensor_tests();
    run_renogy_tests();
    run_zone_manager_tests();
    run_watering_fsm_tests();
    run_water_usage_tracker_tests();

    // exit() terminates the FreeRTOS Linux scheduler threads so the process
    // actually exits.  Without this, vTaskStartScheduler() never returns and
    // the container hangs indefinitely after the tests finish.
    exit(UNITY_END());
}
