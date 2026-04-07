#pragma once
#include <cstdint>

// Hardware pulse counter — wraps the ESP32-C6 PCNT peripheral.
// The concrete implementation (PcntPulseCounter) configures a PCNT unit to
// count rising edges on the flow meter GPIO. This interface exists so the
// FlowMeter class can be tested on the host without the PCNT hardware.
class IPulseCounter {
public:
    virtual ~IPulseCounter() = default;

    // Reset the hardware counter to zero.
    virtual void reset() = 0;

    // Return the current accumulated count since the last reset().
    virtual int32_t getCount() const = 0;
};
