#include "drivers/solenoid_actuator.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

SolenoidActuator::SolenoidActuator(IPwm& pwm, uint32_t pullInMs, uint8_t holdDutyPct)
    : pwm_(pwm)
    , pullInMs_(pullInMs)
    , holdDutyPct_(holdDutyPct)
{}

void SolenoidActuator::open()
{
    pwm_.setDutyPercent(100);
    if (pullInMs_ > 0) {
        vTaskDelay(pdMS_TO_TICKS(pullInMs_));
    }
    pwm_.setDutyPercent(holdDutyPct_);
    open_ = true;
}

void SolenoidActuator::close()
{
    pwm_.stop();
    open_ = false;
}

bool SolenoidActuator::isOpen() const
{
    return open_;
}
