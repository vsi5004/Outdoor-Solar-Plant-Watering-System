#include "hal/ledc_pwm.hpp"
#include "config.hpp"
#include "esp_err.h"

void LedcPwm::initTimer()
{
    ledc_timer_config_t cfg = {};
    cfg.speed_mode      = kMode;
    cfg.duty_resolution = static_cast<ledc_timer_bit_t>(config::ledc::RESOLUTION_BITS);
    cfg.timer_num       = kTimer;
    cfg.freq_hz         = config::ledc::FREQUENCY_HZ;
    cfg.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&cfg));
}

LedcPwm::LedcPwm(int gpioNum, ledc_channel_t channel)
    : channel_(channel)
{
    ledc_channel_config_t cfg = {};
    cfg.gpio_num   = gpioNum;
    cfg.speed_mode = kMode;
    cfg.channel    = channel_;
    cfg.intr_type  = LEDC_INTR_DISABLE;
    cfg.timer_sel  = kTimer;
    cfg.duty       = 0;
    cfg.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&cfg));
}

void LedcPwm::setDutyPercent(uint8_t duty)
{
    const uint32_t raw = (static_cast<uint32_t>(duty) * kMaxDuty + 50u) / 100u;
    ESP_ERROR_CHECK(ledc_set_duty(kMode, channel_, raw));
    ESP_ERROR_CHECK(ledc_update_duty(kMode, channel_));
}

void LedcPwm::stop()
{
    ESP_ERROR_CHECK(ledc_set_duty(kMode, channel_, 0));
    ESP_ERROR_CHECK(ledc_update_duty(kMode, channel_));
}
