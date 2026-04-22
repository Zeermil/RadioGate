#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "PersistentConfig.h"
#include "mesh_types.h"

class LoRaRadio {
public:
    struct Stats {
        bool ready;
        uint32_t rxCount;
        uint32_t txCount;
        uint32_t lastActivityMs;
        int16_t lastRssi;
        float lastSnr;
        char lastError[48];
    };

    LoRaRadio();

    bool begin(const PersistentConfig::RadioProfile& profile);
    bool enqueue(const OutgoingMeshPacket& packet);
    bool pollReceive(ReceivedFrame& frame, uint32_t nowMs);
    void processTx(uint32_t nowMs);
    size_t queueDepth() const;
    const Stats& stats() const;

private:
    bool sendPacket(const OutgoingMeshPacket& packet, uint32_t nowMs);

    SPIClass m_spi;
    OutgoingMeshPacket m_queue[cfg::kMaxOutgoingPackets];
    Stats m_stats;
    uint32_t m_lastTxMs;
};
