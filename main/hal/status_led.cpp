#include "hal/status_led.hpp"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

// WS2812 on GPIO8 (ESP32-C6-DevKitC-1 onboard RGB LED).
static constexpr int     LED_GPIO      = 8;
static constexpr uint8_t LED_INTENSITY = 20; // 0-255, keep low indoors

static led_strip_handle_t     s_strip  = nullptr;
static std::atomic<StatusLed::State> s_state{StatusLed::State::Connecting};

void StatusLed::init()
{
    led_strip_config_t cfg = {
        .strip_gpio_num   = LED_GPIO,
        .max_leds         = 1,
        .led_model        = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10'000'000, // 10 MHz
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt, &s_strip));
    led_strip_clear(s_strip);
}

void StatusLed::setState(State s)
{
    s_state.store(s, std::memory_order_relaxed);
}

void StatusLed::setPixel(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

void StatusLed::off()
{
    led_strip_clear(s_strip);
}

// Simple tick counter incremented every 100 ms.
void StatusLed::runTask(void* /*arg*/)
{
    uint32_t tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ++tick;

        switch (s_state.load(std::memory_order_relaxed)) {

        case State::Connecting:
            // Blue blink: on 200 ms, off 800 ms (2 ticks on, 8 ticks off).
            if ((tick % 10) < 2) {
                setPixel(0, 0, LED_INTENSITY);
            } else {
                off();
            }
            break;

        case State::Joined:
            // Green heartbeat: 100 ms flash every 5 s (1 tick on, 49 ticks off)
            if ((tick % 50) == 0) {
                setPixel(0, LED_INTENSITY, 0);
            } else if ((tick % 50) == 1) {
                off();
            }
            break;

        case State::Watering:
            // Solid cyan while watering.
            setPixel(0, LED_INTENSITY / 2, LED_INTENSITY);
            break;

        case State::Fault:
            // Red: 100 ms on, 200 ms off (1 tick on, 2 ticks off).
            if ((tick % 3) == 0) {
                setPixel(LED_INTENSITY, 0, 0);
            } else {
                off();
            }
            break;
        }
    }
}
