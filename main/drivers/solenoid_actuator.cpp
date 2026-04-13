#include "drivers/solenoid_actuator.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

SolenoidActuator::SolenoidActuator(IPwm& pwm, uint32_t pullInMs, uint8_t holdDutyPct, uint8_t pullInDutyPct)
    : pwm_(pwm)
    , pullInMs_(pullInMs)
    , holdDutyPct_(holdDutyPct)
    , pullInDutyPct_(pullInDutyPct)
{}

void SolenoidActuator::open()
{
    pwm_.setDutyPercent(pullInDutyPct_);
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
