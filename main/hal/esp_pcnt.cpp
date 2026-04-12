#include "hal/esp_pcnt.hpp"
#include "esp_err.h"

EspPcnt::EspPcnt(int gpioNum)
{
    pcnt_unit_config_t unit_cfg = {
        .low_limit  = -32768,
        .high_limit =  32767,
        .flags      = { .accum_count = 1 }, // accumulate across overflow
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &unit_));

    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num  = gpioNum,
        .level_gpio_num = -1,
    };
    ESP_ERROR_CHECK(pcnt_new_channel(unit_, &chan_cfg, &chan_));

    // Count on rising edge; hold count on falling edge.
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
        chan_,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // rising
        PCNT_CHANNEL_EDGE_ACTION_HOLD));    // falling

    ESP_ERROR_CHECK(pcnt_unit_enable(unit_));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unit_));
    ESP_ERROR_CHECK(pcnt_unit_start(unit_));
}

EspPcnt::~EspPcnt()
{
    pcnt_unit_stop(unit_);
    pcnt_unit_disable(unit_);
    pcnt_del_channel(chan_);
    pcnt_del_unit(unit_);
}

void EspPcnt::reset()
{
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unit_));
}

int32_t EspPcnt::getCount() const
{
    int count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(unit_, &count));
    return static_cast<int32_t>(count);
}
