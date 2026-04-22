#include "PersistentConfig.h"

#include "config.h"

PersistentConfig::PersistentConfig() : m_radioProfile{} {
    m_radioProfile.frequencyHz = RADIO_PROFILE_DEFAULT;
    m_radioProfile.bandwidthHz = cfg::kLoRaBandwidth;
    m_radioProfile.spreadingFactor = cfg::kLoRaSpreadingFactor;
    m_radioProfile.codingRate = cfg::kLoRaCodingRate;
    m_radioProfile.txPower = cfg::kLoRaTxPower;
    m_radioProfile.syncWord = cfg::kLoRaSyncWord;
}

bool PersistentConfig::begin() {
    if (!m_preferences.begin(cfg::kPreferenceNamespace, false)) {
        return false;
    }
    load();
    return true;
}

void PersistentConfig::end() {
    m_preferences.end();
}

void PersistentConfig::load() {
    const uint32_t frequency = m_preferences.getULong("rf_freq", defaultFrequency());
    m_radioProfile.frequencyHz = frequency;
}

const PersistentConfig::RadioProfile& PersistentConfig::radioProfile() const {
    return m_radioProfile;
}

bool PersistentConfig::setFrequency(uint32_t frequencyHz) {
    m_radioProfile.frequencyHz = frequencyHz;
    return m_preferences.putULong("rf_freq", frequencyHz) > 0;
}

uint32_t PersistentConfig::defaultFrequency() const {
    return RADIO_PROFILE_DEFAULT;
}
