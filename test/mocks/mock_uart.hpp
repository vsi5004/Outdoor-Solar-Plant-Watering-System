#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "hal/iuart.hpp"

// Test double for IUart.
//
// Enqueue bytes to be returned by read() with enqueueRx().
// Inspect lastTx_ after write() to verify the bytes the SUT transmitted.
//
// discardOnFlush (default false):
//   false — flushRx() only increments flushCount_; it does NOT clear pre-loaded
//           test data.  Use this when you pre-load all responses up front
//           (e.g. multiple Modbus requests in one poll() call).
//   true  — flushRx() behaves like the real UART: it discards all buffered bytes.
//           Use this when testing that stale data is correctly discarded.
class MockUart : public IUart {
public:
    bool discardOnFlush = false;

    void write(const uint8_t* data, size_t len) override
    {
        lastTx_.assign(data, data + len);
        writeCallCount_++;
    }

    size_t read(uint8_t* buf, size_t len, uint32_t /*timeoutMs*/) override
    {
        const size_t n = std::min(len, rxBuffer_.size());
        if (n > 0) {
            std::memcpy(buf, rxBuffer_.data(), n);
            rxBuffer_.erase(rxBuffer_.begin(),
                            rxBuffer_.begin() + static_cast<std::ptrdiff_t>(n));
        }
        readCallCount_++;
        return n;
    }

    void flushRx() override
    {
        if (discardOnFlush) {
            rxBuffer_.clear();
        }
        flushCount_++;
    }

    // Enqueue bytes that the next read() call(s) will return.
    void enqueueRx(const std::vector<uint8_t>& data)
    {
        rxBuffer_.insert(rxBuffer_.end(), data.begin(), data.end());
    }

    // ── Test inspection ──────────────────────────────────────────────────────
    std::vector<uint8_t> lastTx_;
    int                  writeCallCount_ = 0;
    int                  readCallCount_  = 0;
    int                  flushCount_     = 0;

private:
    std::vector<uint8_t> rxBuffer_;
};
