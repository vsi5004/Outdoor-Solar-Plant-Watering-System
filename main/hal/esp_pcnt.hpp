#pragma once
#include "hal/ipulse_counter.hpp"
#include "driver/pulse_cnt.h"

// IPulseCounter backed by the ESP-IDF PCNT peripheral.
// Counts rising edges on a single GPIO.
class EspPcnt : public IPulseCounter {
public:
    explicit EspPcnt(int gpioNum);

    void    reset()          override;
    int32_t getCount() const override;

private:
    pcnt_unit_handle_t   unit_  = nullptr;
    pcnt_channel_handle_t chan_ = nullptr;
};
