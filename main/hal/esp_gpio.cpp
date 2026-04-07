#include "hal/esp_gpio.hpp"
#include "esp_err.h"

EspGpio::EspGpio(int gpioNum)
    : pin_(static_cast<gpio_num_t>(gpioNum))
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin_),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    // Default low — caller sets the desired initial level after construction.
    ESP_ERROR_CHECK(gpio_set_level(pin_, 0));
}

void EspGpio::setHigh()
{
    ESP_ERROR_CHECK(gpio_set_level(pin_, 1));
    level_ = true;
}

void EspGpio::setLow()
{
    ESP_ERROR_CHECK(gpio_set_level(pin_, 0));
    level_ = false;
}

bool EspGpio::getLevel() const { return level_; }
