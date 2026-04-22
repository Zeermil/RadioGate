#pragma once

#include <Arduino.h>
#include <Preferences.h>

class PersistentConfig {
public:
    struct RadioProfile {
        uint32_t frequencyHz;
        uint32_t bandwidthHz;
        uint8_t spreadingFactor;
        uint8_t codingRate;
        uint8_t txPower;
        uint8_t syncWord;
    };

    PersistentConfig();

    bool begin();
    void end();

    const RadioProfile& radioProfile() const;
    bool setFrequency(uint32_t frequencyHz);
    uint32_t defaultFrequency() const;

private:
    void load();

    Preferences m_preferences;
    RadioProfile m_radioProfile;
};
