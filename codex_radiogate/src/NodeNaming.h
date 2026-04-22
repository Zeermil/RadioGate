#pragma once

#include <Arduino.h>

class NodeNaming {
public:
    bool chooseDisplayName(char* destination, size_t destinationSize) const;
};
