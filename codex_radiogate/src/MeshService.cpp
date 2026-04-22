#include "MeshService.h"

#include <string.h>

#include "entities/routingTable/RouteNode.h"
#include "services/RoutingTableService.h"
#include "utils.h"

MeshService::MeshService()
    : m_spi(HSPI),
      m_radio(LoraMesher::getInstance()),
      m_receiveTaskHandle(nullptr),
      m_rxQueue(nullptr),
      m_stats{} {
    utils::safeCopy(m_stats.lastError, sizeof(m_stats.lastError), "none");
}

MeshService::~MeshService() {
    if (m_rxQueue != nullptr) {
        vQueueDelete(m_rxQueue);
        m_rxQueue = nullptr;
    }
}

bool MeshService::begin(const PersistentConfig::RadioProfile& profile) {
    m_spi.begin(cfg::kPinLoRaSck, cfg::kPinLoRaMiso, cfg::kPinLoRaMosi, cfg::kPinLoRaSs);

    if (m_rxQueue == nullptr) {
        m_rxQueue = xQueueCreate(cfg::kMeshRxQueueSize, sizeof(MeshReceivedFrame));
    }
    if (m_rxQueue == nullptr) {
        setLastError("mesh queue alloc failed");
        return false;
    }

    LoraMesher::LoraMesherConfig config;
    config.loraCs = cfg::kPinLoRaSs;
    config.loraRst = cfg::kPinLoRaRst;
    config.loraIrq = cfg::kPinLoRaDio0;
    config.loraIo1 = cfg::kPinLoRaDio1;
    config.spi = &m_spi;
    config.module = profile.frequencyHz < 700000000UL ? LoraMesher::LoraModules::SX1278_MOD : LoraMesher::LoraModules::SX1276_MOD;
    config.freq = static_cast<float>(profile.frequencyHz) / 1000000.0F;
    config.bw = static_cast<float>(profile.bandwidthHz) / 1000.0F;
    config.sf = profile.spreadingFactor;
    config.cr = profile.codingRate;
    config.syncWord = profile.syncWord;
    config.power = static_cast<int8_t>(profile.txPower);
    config.max_packet_size = cfg::kMaxLoRaPayload;

    m_radio.begin(config);

    if (m_receiveTaskHandle == nullptr) {
        BaseType_t result = xTaskCreatePinnedToCore(
            receiveTaskTrampoline,
            "LM-AppRx",
            cfg::kMeshReceiveTaskStackSize,
            this,
            2,
            &m_receiveTaskHandle,
            1);
        if (result != pdPASS) {
            setLastError("mesh task create failed");
            return false;
        }
    }

    m_radio.setReceiveAppDataTaskHandle(m_receiveTaskHandle);
    m_radio.start();

    delay(50);
    m_stats.meshAddress = m_radio.getLocalAddress();
    m_stats.ready = m_stats.meshAddress != 0;
    m_stats.lastActivityMs = millis();
    if (!m_stats.ready) {
        setLastError("mesh address unavailable");
    } else {
        utils::safeCopy(m_stats.lastError, sizeof(m_stats.lastError), "none");
    }

    Serial.printf("[LM-BOOT] addr=%04X dio0=%u dio1=%u freq=%.3f module=%s\n",
                  static_cast<unsigned>(m_stats.meshAddress),
                  static_cast<unsigned>(cfg::kPinLoRaDio0),
                  static_cast<unsigned>(cfg::kPinLoRaDio1),
                  static_cast<double>(config.freq),
                  config.module == LoraMesher::LoraModules::SX1278_MOD ? "SX1278" : "SX1276");
    return m_stats.ready;
}

void MeshService::loop(uint32_t nowMs) {
    if (m_stats.ready) {
        m_stats.meshAddress = m_radio.getLocalAddress();
        m_stats.lastActivityMs = max(m_stats.lastActivityMs, nowMs);
    }
}

bool MeshService::sendBroadcast(const uint8_t* payload, size_t payloadLength, uint32_t nowMs) {
    if (!m_stats.ready || payload == nullptr || payloadLength == 0 || payloadLength > cfg::kMaxMeshAppPayload) {
        setLastError("broadcast payload invalid");
        return false;
    }
    m_radio.sendPacket(BROADCAST_ADDR, payload, payloadLength);
    ++m_stats.txCount;
    m_stats.lastActivityMs = nowMs;
    utils::safeCopy(m_stats.lastError, sizeof(m_stats.lastError), "none");
    return true;
}

bool MeshService::sendReliable(uint16_t dstAddress, const uint8_t* payload, size_t payloadLength, uint32_t nowMs) {
    if (!m_stats.ready || dstAddress == 0 || payload == nullptr || payloadLength == 0 || payloadLength > cfg::kMaxMeshAppPayload) {
        setLastError("reliable payload invalid");
        return false;
    }
    RouteNode* route = RoutingTableService::findNode(dstAddress);
    if (route == nullptr) {
        setLastError("route not found");
        return false;
    }
    m_radio.sendReliablePacket(dstAddress, const_cast<uint8_t*>(payload), payloadLength);
    ++m_stats.txCount;
    m_stats.lastActivityMs = nowMs;
    utils::safeCopy(m_stats.lastError, sizeof(m_stats.lastError), "none");
    return true;
}

bool MeshService::popReceived(MeshReceivedFrame& frame) {
    if (m_rxQueue == nullptr) {
        return false;
    }
    return xQueueReceive(m_rxQueue, &frame, 0) == pdTRUE;
}

uint16_t MeshService::localAddress() const {
    return m_stats.meshAddress;
}

size_t MeshService::routeCount() const {
    return const_cast<LoraMesher&>(m_radio).routingTableSize();
}

size_t MeshService::neighborCount() const {
    size_t count = 0;
    LM_LinkedList<RouteNode>* copy = const_cast<LoraMesher&>(m_radio).routingTableListCopy();
    if (copy != nullptr) {
        if (copy->moveToStart()) {
            do {
                RouteNode* node = copy->getCurrent();
                if (node != nullptr && node->networkNode.metric == 1) {
                    ++count;
                }
            } while (copy->next());
        }
        delete copy;
    }
    return count;
}

size_t MeshService::queueDepth() const {
    return const_cast<LoraMesher&>(m_radio).getSendQueueSize();
}

const MeshService::Stats& MeshService::stats() const {
    return m_stats;
}

void MeshService::receiveTaskTrampoline(void* context) {
    if (context != nullptr) {
        static_cast<MeshService*>(context)->receiveTask();
    }
    vTaskDelete(nullptr);
}

void MeshService::receiveTask() {
    for (;;) {
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        while (m_radio.getReceivedQueueSize() > 0) {
            AppPacket<uint8_t>* packet = m_radio.getNextAppPacket<uint8_t>();
            if (packet == nullptr) {
                continue;
            }

            MeshReceivedFrame frame{};
            frame.srcAddress = packet->src;
            frame.dstAddress = packet->dst;
            frame.payloadLength = min(static_cast<size_t>(packet->payloadSize), static_cast<size_t>(cfg::kMaxMeshAppPayload));
            memcpy(frame.payload, packet->payload, frame.payloadLength);
            m_radio.deletePacket(packet);

            if (xQueueSend(m_rxQueue, &frame, 0) != pdTRUE) {
                setLastError("mesh rx queue full");
                continue;
            }

            ++m_stats.rxCount;
            m_stats.lastActivityMs = millis();
            utils::safeCopy(m_stats.lastError, sizeof(m_stats.lastError), "none");
        }
    }
}

void MeshService::setLastError(const char* message) {
    utils::safeCopy(m_stats.lastError, sizeof(m_stats.lastError), message);
}
