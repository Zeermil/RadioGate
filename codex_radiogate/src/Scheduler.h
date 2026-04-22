#pragma once

#include <Arduino.h>

class Scheduler {
public:
    Scheduler();

    bool every(uint32_t intervalMs, uint32_t& marker, uint32_t nowMs) const;
};
