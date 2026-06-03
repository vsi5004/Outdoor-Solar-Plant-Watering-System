#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Tracks self-originated attribute writes so command handlers can ignore the
// corresponding local echo delivered by the Zigbee stack.
class AttrEchoSuppressor {
public:
    static constexpr size_t kMaxValueSize = 8u;
    static constexpr size_t kCapacity     = 8u;

    struct Key {
        uint8_t  endpoint;
        uint16_t clusterId;
        uint16_t attrId;

        constexpr bool operator==(const Key& other) const
        {
            return endpoint == other.endpoint &&
                   clusterId == other.clusterId &&
                   attrId == other.attrId;
        }
    };

    AttrEchoSuppressor();

    void reset();

    template <typename T>
    bool remember(const Key& key, const T& value)
    {
        return rememberBytes(key, &value, sizeof(T));
    }

    template <typename T>
    bool consume(const Key& key, const T& value)
    {
        return consumeBytes(key, &value, sizeof(T));
    }

    void clear(const Key& key);

private:
    struct Slot {
        bool active = false;
        Key key     = {};
        std::array<uint8_t, kMaxValueSize> value = {};
        size_t valueSize = 0u;
    };

    std::array<Slot, kCapacity> slots_ = {};

    bool rememberBytes(const Key& key, const void* value, size_t valueSize);
    bool consumeBytes(const Key& key, const void* value, size_t valueSize);
    static bool valuesEqual(const Slot& slot, const void* value, size_t valueSize);
};
