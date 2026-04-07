#pragma once

// Single digital output pin.
// Used for DRV_MASTER_EN — the shared emergency-stop line across all three
// BTS7960 boards. HIGH = drivers enabled, LOW = all outputs cut instantly.
class IGpio {
public:
    virtual ~IGpio() = default;

    virtual void setHigh() = 0;
    virtual void setLow()  = 0;

    // Returns the last level driven (not necessarily read back from hardware).
    virtual bool getLevel() const = 0;
};
