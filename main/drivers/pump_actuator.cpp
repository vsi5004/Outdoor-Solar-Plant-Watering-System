#include "drivers/pump_actuator.hpp"

PumpActuator::PumpActuator(IPwm& rpwm)
    : rpwm_(rpwm)
{}

void PumpActuator::setSpeed(uint8_t pct)
{
    rpwm_.setDutyPercent(pct);
}

void PumpActuator::stop()
{
    rpwm_.stop();
}
