#pragma once
#include "drivers/renogy_data.hpp"

class IRenogyMonitor {
public:
    virtual ~IRenogyMonitor() = default;
    virtual RenogyData getData() const = 0;
};
