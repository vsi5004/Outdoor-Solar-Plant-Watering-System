#pragma once
#include "drivers/renogy_data.hpp"

class IRenogyMonitor {
public:
    virtual ~IRenogyMonitor() = default;
    virtual RenogyData getData() const = 0;

    // Enable or disable the controller's 12V load output (register 0x010A).
    // Returns true if the controller acknowledged the command.
    virtual bool setLoad(bool on) = 0;
};
