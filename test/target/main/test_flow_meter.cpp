#include "unity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/esp_pcnt.hpp"
#include "drivers/flow_meter.hpp"
#include "config.hpp"

static const char* TAG = "test_flow_meter";

// Duration of the main monitoring window.
static constexpr uint32_t MONITOR_DURATION_MS  = 15'000;
static constexpr uint32_t SAMPLE_INTERVAL_MS   =    500;
static constexpr uint32_t SAMPLES              = MONITOR_DURATION_MS / SAMPLE_INTERVAL_MS;

void register_flow_meter_tests() {}

// ---------------------------------------------------------------------------
// HIL test — flow meter pulse counting (GPIO6, PCNT)
//
// Blow into or run water through the flow meter during each test window.
// ---------------------------------------------------------------------------

TEST_CASE("flow meter: pulse rate and volume over 15 s", "[flow_meter]")
{
    EspPcnt   pcnt(config::pins::FLOW_METER);
    FlowMeter flow(pcnt);

    flow.reset();

    ESP_LOGI(TAG, "GPIO %d  ML_PER_PULSE=%.3f",
             config::pins::FLOW_METER, config::flow::ML_PER_PULSE);
    ESP_LOGI(TAG, "Blow into or run water through the flow meter for %lu s...",
             (unsigned long)(MONITOR_DURATION_MS / 1000));
    ESP_LOGI(TAG, "%-6s  %-8s  %-6s  %-9s  %-8s",
             "t (s)", "pulses", "delta", "p/s", "mL");

    int32_t  lastPulses  = 0;
    float    peakRatePs  = 0.0f;
    uint32_t activeSamples = 0;

    for (uint32_t i = 0; i < SAMPLES; ++i) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));

        const int32_t total   = flow.getPulses();
        const int32_t delta   = total - lastPulses;
        const float   ratePs  = static_cast<float>(delta) /
                                (static_cast<float>(SAMPLE_INTERVAL_MS) / 1000.0f);
        const float   ml      = flow.getMilliliters();

        ESP_LOGI(TAG, "%-6lu  %-8ld  %-6ld  %-9.1f  %-8.1f",
                 (unsigned long)((i + 1) * SAMPLE_INTERVAL_MS / 1000),
                 (long)total, (long)delta, ratePs, ml);

        if (ratePs > peakRatePs) peakRatePs = ratePs;
        if (delta > 0)           ++activeSamples;
        lastPulses = total;
    }

    const int32_t totalPulses = flow.getPulses();
    const float   totalMl     = flow.getMilliliters();

    ESP_LOGI(TAG, "--- Summary ---");
    ESP_LOGI(TAG, "Total pulses  : %ld", (long)totalPulses);
    ESP_LOGI(TAG, "Total volume  : %.1f mL", totalMl);
    ESP_LOGI(TAG, "Peak rate     : %.1f p/s", peakRatePs);
    ESP_LOGI(TAG, "Active samples: %lu / %lu",
             (unsigned long)activeSamples, (unsigned long)SAMPLES);

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, totalPulses,
        "No pulses detected — check GPIO6 wiring and flow meter connection");
}

TEST_CASE("flow meter: reset clears pulse count", "[flow_meter]")
{
    EspPcnt   pcnt(config::pins::FLOW_METER);
    FlowMeter flow(pcnt);

    flow.reset();

    ESP_LOGI(TAG, "Blow into the flow meter for 5 s to generate pulses...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    const int32_t beforeReset = flow.getPulses();
    ESP_LOGI(TAG, "Pulses before reset: %ld", (long)beforeReset);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, beforeReset,
        "No pulses detected before reset — blow harder or check wiring");

    flow.reset();
    const int32_t afterReset = flow.getPulses();
    ESP_LOGI(TAG, "Pulses after reset:  %ld", (long)afterReset);
    TEST_ASSERT_EQUAL_MESSAGE(0, afterReset, "reset() did not clear the pulse count");
}
