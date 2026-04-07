#pragma once
#include "hal/ipwm.hpp"
#include "driver/ledc.h"

// IPwm backed by the ESP-IDF LEDC peripheral.
//
// All channels share LEDC_TIMER_0 / LEDC_LOW_SPEED_MODE.
// The timer must be configured once in app_main before constructing any
// LedcPwm instance (call LedcPwm::initTimer()).
class LedcPwm : public IPwm {
public:
    // Configure the shared LEDC timer.  Call once before any LedcPwm is built.
    static void initTimer();

    // gpioNum  — physical GPIO (e.g. config::pins::PUMP_RPWM)
    // channel  — LEDC_CHANNEL_0 … LEDC_CHANNEL_5
    LedcPwm(int gpioNum, ledc_channel_t channel);

    void setDutyPercent(uint8_t duty) override;
    void stop()                       override;

private:
    ledc_channel_t channel_;

    static constexpr ledc_mode_t      kMode   = LEDC_LOW_SPEED_MODE;
    static constexpr ledc_timer_t     kTimer  = LEDC_TIMER_0;
    static constexpr uint32_t         kMaxDuty = (1u << 10) - 1u; // 10-bit = 1023
};
