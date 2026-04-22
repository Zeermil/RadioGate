#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <freertos/queue.h>

#include "PersistentConfig.h"
#include "LoraMesher.h"
#include "config.h"

struct MeshReceivedFrame {
    uint16_t srcAddress;
    uint16_t dstAddress;
    size_t payloadLength;
    uint8_t payload[cfg::kMaxMeshAppPayload];
};

class MeshService {
public:
    struct Stats {
        bool ready;
        uint16_t meshAddress;
        uint32_t txCount;
        uint32_t rxCount;
        uint32_t lastActivityMs;
        int16_t lastRssi;
        float lastSnr;
        char lastError[48];
    };

    MeshService();
    ~MeshService();

    bool begin(const PersistentConfig::RadioProfile& profile);
    void loop(uint32_t nowMs);

    bool sendBroadcast(const uint8_t* payload, size_t payloadLength, uint32_t nowMs);
    bool sendReliable(uint16_t dstAddress, const uint8_t* payload, size_t payloadLength, uint32_t nowMs);
    bool popReceived(MeshReceivedFrame& frame);

    uint16_t localAddress() const;
    size_t routeCount() const;
    size_t neighborCount() const;
    size_t queueDepth() const;
    const Stats& stats() const;

private:
    static void receiveTaskTrampoline(void* context);
    void receiveTask();
    void setLastError(const char* message);

    SPIClass m_spi;
    LoraMesher& m_radio;
    TaskHandle_t m_receiveTaskHandle;
    QueueHandle_t m_rxQueue;
    Stats m_stats;
};
