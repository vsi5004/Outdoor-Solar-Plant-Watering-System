#include "zb/attr_echo_suppressor.hpp"

#include <cstring>

AttrEchoSuppressor::AttrEchoSuppressor()
{
    reset();
}

void AttrEchoSuppressor::reset()
{
    for (auto& slot : slots_) {
        slot.active = false;
        slot.key = {};
        slot.value.fill(0u);
        slot.valueSize = 0u;
    }
}

void AttrEchoSuppressor::clear(const Key& key)
{
    for (auto& slot : slots_) {
        if (!slot.active || !(slot.key == key)) {
            continue;
        }
        slot.active = false;
        slot.value.fill(0u);
        slot.valueSize = 0u;
        return;
    }
}

bool AttrEchoSuppressor::rememberBytes(const Key& key, const void* value, size_t valueSize)
{
    if (value == nullptr || valueSize == 0u || valueSize > kMaxValueSize) {
        return false;
    }

    Slot* freeSlot = nullptr;
    for (auto& slot : slots_) {
        if (slot.active && slot.key == key) {
            memcpy(slot.value.data(), value, valueSize);
            if (valueSize < slot.value.size()) {
                memset(slot.value.data() + valueSize, 0, slot.value.size() - valueSize);
            }
            slot.valueSize = valueSize;
            return true;
        }
        if (!slot.active && freeSlot == nullptr) {
            freeSlot = &slot;
        }
    }

    if (freeSlot == nullptr) {
        return false;
    }

    freeSlot->active = true;
    freeSlot->key = key;
    memcpy(freeSlot->value.data(), value, valueSize);
    if (valueSize < freeSlot->value.size()) {
        memset(freeSlot->value.data() + valueSize, 0, freeSlot->value.size() - valueSize);
    }
    freeSlot->valueSize = valueSize;
    return true;
}

bool AttrEchoSuppressor::consumeBytes(const Key& key, const void* value, size_t valueSize)
{
    if (value == nullptr || valueSize == 0u || valueSize > kMaxValueSize) {
        return false;
    }

    for (auto& slot : slots_) {
        if (!slot.active || !(slot.key == key) || !valuesEqual(slot, value, valueSize)) {
            continue;
        }
        slot.active = false;
        slot.value.fill(0u);
        slot.valueSize = 0u;
        return true;
    }

    return false;
}

bool AttrEchoSuppressor::valuesEqual(const Slot& slot, const void* value, size_t valueSize)
{
    return slot.valueSize == valueSize &&
           memcmp(slot.value.data(), value, valueSize) == 0;
}
