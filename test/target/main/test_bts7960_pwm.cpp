#include "unity.h"
#include "unity_test_runner.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/ledc_pwm.hpp"
#include "hal/esp_gpio.hpp"
#include "config.hpp"

static const char *TAG = "test_bts7960_pwm";

// =============================================================================
// Characterization parameters — adjust before running.
// =============================================================================

// ── Pump ──────────────────────────────────────────────────────────────────────
static constexpr uint8_t PUMP_DUTY_PCT = 50;   // duty % to apply
static constexpr uint32_t PUMP_RUN_MS = 5'000; // total run duration

// ── Solenoids (shared across all five channels) ───────────────────────────────
static constexpr uint8_t SOL_PULL_IN_PCT = 85;  // pull-in duty %
static constexpr uint8_t SOL_HOLD_PCT = 20;     // hold duty % under evaluation
static constexpr uint32_t SOL_PULL_IN_MS = 100; // pull-in phase duration
static constexpr uint32_t SOL_HOLD_MS = 3'000;  // hold observation window

// Force the linker to pull in this translation unit.
void register_bts7960_pwm_tests() {}

// =============================================================================
// Pump test
// =============================================================================

TEST_CASE("pump: run at configurable duty for fixed duration", "[pump]")
{
    LedcPwm pwm(config::pins::PUMP_RPWM,
                static_cast<ledc_channel_t>(config::ledc::PUMP_CH));
    EspGpio masterEn(config::pins::DRV_MASTER_EN);

    ESP_LOGI(TAG, "Pump HIL: duty=%u%%  run=%lu ms",
             PUMP_DUTY_PCT, (unsigned long)PUMP_RUN_MS);

    masterEn.setHigh();
    pwm.setDutyPercent(PUMP_DUTY_PCT);
    vTaskDelay(pdMS_TO_TICKS(PUMP_RUN_MS));
    pwm.stop();
    masterEn.setLow();

    ESP_LOGI(TAG, "Pump run complete");
}

// =============================================================================
// Solenoid tests — one per channel, identical structure.
//
// Each test drives the pull-in phase at SOL_PULL_IN_PCT for SOL_PULL_IN_MS,
// then holds at SOL_HOLD_PCT for SOL_HOLD_MS.
// =============================================================================

#define SOL_TEST(label, tag, pwmPin, ledcCh)                        \
    TEST_CASE("solenoid " label ": pull-in then hold", "[" tag "]") \
    {                                                               \
        LedcPwm pwm(pwmPin, static_cast<ledc_channel_t>(ledcCh));   \
        EspGpio masterEn(config::pins::DRV_MASTER_EN);              \
                                                                    \
        ESP_LOGI(TAG,                                               \
                 "Solenoid " label " HIL: pull_in=%u%% for %lu ms"  \
                 "  hold=%u%% for %lu ms",                          \
                 SOL_PULL_IN_PCT, (unsigned long)SOL_PULL_IN_MS,    \
                 SOL_HOLD_PCT, (unsigned long)SOL_HOLD_MS);         \
                                                                    \
        masterEn.setHigh();                                         \
                                                                    \
        pwm.setDutyPercent(SOL_PULL_IN_PCT);                        \
        vTaskDelay(pdMS_TO_TICKS(SOL_PULL_IN_MS));                  \
                                                                    \
        pwm.setDutyPercent(SOL_HOLD_PCT);                           \
        vTaskDelay(pdMS_TO_TICKS(SOL_HOLD_MS));                     \
                                                                    \
        pwm.stop();                                                 \
        masterEn.setLow();                                          \
                                                                    \
        ESP_LOGI(TAG, "Solenoid " label " run complete");           \
    }

SOL_TEST("1", "sol1", config::pins::SOL1_LPWM, config::ledc::SOL1_CH)
SOL_TEST("2", "sol2", config::pins::SOL2_RPWM, config::ledc::SOL2_CH)
SOL_TEST("3", "sol3", config::pins::SOL3_LPWM, config::ledc::SOL3_CH)
SOL_TEST("4", "sol4", config::pins::SOL4_RPWM, config::ledc::SOL4_CH)
SOL_TEST("5", "sol5", config::pins::SOL5_RPWM, config::ledc::SOL5_CH)
