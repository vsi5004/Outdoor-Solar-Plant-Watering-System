#pragma once
#include "hal/igpio.hpp"
#include "driver/gpio.h"

// IGpio backed by a single ESP-IDF output GPIO.
class EspGpio : public IGpio {
public:
    explicit EspGpio(int gpioNum);

    void setHigh()        override;
    void setLow()         override;
    bool getLevel() const override;

private:
    gpio_num_t pin_;
    bool       level_ = false;
};
