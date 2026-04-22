#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "ClientRegistry.h"
#include "HttpApiServer.h"
#include "MeshProtocol.h"
#include "MeshService.h"
#include "NodeIdentity.h"
#include "NodeNaming.h"
#include "PendingMessageQueue.h"
#include "PersistentConfig.h"
#include "SessionManager.h"
#include "WebSocketGateway.h"
#include "WiFiApService.h"

class NodeController {
public:
    struct ConnectReply {
        uint16_t httpStatus;
        char status[16];
        char code[40];
        char message[96];
        char username[cfg::kUsernameLength];
        char sessionToken[cfg::kTokenLength];
        uint32_t retryAfterMs;
        bool newClient;
    };

    struct SendReply {
        uint16_t httpStatus;
        char status[16];
        char code[40];
        char message[96];
        char messageId[cfg::kMessageIdLength];
    };

    struct DisconnectReply {
        uint16_t httpStatus;
        char status[16];
        char code[40];
        char message[96];
        char username[cfg::kUsernameLength];
    };

    NodeController();

    bool begin();
    void loop();
    uint32_t captureNowMs();
    uint32_t monotonicNowMs() const;

    ConnectReply handleConnect(const char* username,
                               const char* stableClientId,
                               const char* mac,
                               uint32_t nowMs);
    uint16_t buildClientsResponse(const char* username,
                                  const char* token,
                                  JsonDocument& document,
                                  uint32_t nowMs);
    SendReply handleSend(const char* sessionToken,
                         const char* toUsername,
                         const char* clientMessageId,
                         const char* content,
                         uint32_t nowMs);
    DisconnectReply handleDisconnect(const char* sessionToken, uint32_t nowMs);
    void fillStatus(JsonDocument& document, uint32_t nowMs) const;
    const NodeIdentityInfo& identity() const { return m_identity.info(); }

    bool authorizeWebSocket(const char* username,
                            const char* token,
                            uint32_t clientId,
                            uint32_t nowMs);
    void handleWebSocketPong(uint32_t clientId, uint32_t nowMs);
    void handleWebSocketText(uint32_t clientId, const char* payload, size_t length, uint32_t nowMs);
    void handleWebSocketDisconnect(uint32_t clientId, uint32_t nowMs);

private:
    struct RemoteNodeRecord {
        bool used;
        uint16_t meshAddress;
        uint32_t nodeHash;
        char nodeId[cfg::kNodeIdLength];
        char displayName[cfg::kDisplayNameLength];
        uint8_t lastUserCount;
        uint32_t lastPresenceMs;
    };

    void processMeshFrames(uint32_t nowMs);
    void processPresenceBroadcast(uint32_t nowMs);
    void processRemoteNodeMaintenance(uint32_t nowMs);
    void processPendingMessages(uint32_t nowMs);
    void processSessionMaintenance(uint32_t nowMs);

    void handleMeshFrame(const MeshReceivedFrame& frame, uint32_t nowMs);
    void handlePresenceSnapshot(const meshproto::PresenceSnapshot& snapshot, uint16_t srcAddress, uint32_t nowMs);
    void handleChatEnvelope(const meshproto::ChatEnvelope& envelope, uint16_t srcAddress, uint32_t nowMs);
    void handleDeliveryAck(const meshproto::DeliveryAck& ack, uint16_t srcAddress, uint32_t nowMs);
    void handleDeliveryFail(const meshproto::DeliveryFail& fail, uint16_t srcAddress, uint32_t nowMs);

    bool broadcastPresenceSnapshot(uint32_t nowMs);
    bool sendDeliveryAck(uint16_t dstMeshAddress, const char* messageId, uint32_t nowMs);
    bool sendDeliveryFail(uint16_t dstMeshAddress, const char* messageId, DeliveryFailure failure, uint32_t nowMs);

    void sendNetworkEvent(const char* event, const ClientRecord& record, bool online);
    WebSocketGateway::SendResult sendMessageStatusToUser(const char* username,
                                                         const char* messageId,
                                                         const char* status,
                                                         const char* reason);
    WebSocketGateway::SendResult sendIncomingMessageToUser(const char* username,
                                                           const char* messageId,
                                                           const char* fromUsername,
                                                           const char* content,
                                                           uint32_t timestampMs);
    WebSocketGateway::SendResult pushTextToLocalUser(const char* username, const char* payload, uint32_t nowMs);

    bool clientOnlineState(const ClientRecord& record) const;
    bool isLocalClientReachable(const ClientRecord& record) const;
    size_t countLocalReachableClients() const;
    size_t countRemoteClients() const;

    RemoteNodeRecord* findRemoteNodeByHash(uint32_t nodeHash);
    const RemoteNodeRecord* findRemoteNodeByHash(uint32_t nodeHash) const;
    RemoteNodeRecord* upsertRemoteNode(uint32_t nodeHash,
                                       uint16_t meshAddress,
                                       const char* nodeId,
                                       const char* displayName,
                                       uint32_t nowMs);
    void removeRemoteUsersForNode(uint32_t nodeHash);
    void rememberMeshActivity(const char* type, const char* from, const char* to);
    void setLastError(const char* errorText);
    uint32_t nextMessageSequence();

    PersistentConfig m_config;
    NodeIdentity m_identity;
    NodeNaming m_naming;
    WiFiApService m_wifiAp;
    ClientRegistry m_clientRegistry;
    SessionManager m_sessionManager;
    PendingMessageQueue m_pendingMessages;
    MeshService m_mesh;
    HttpApiServer m_httpApi;
    WebSocketGateway m_webSocket;
    uint32_t m_monotonicNowMs;
    uint32_t m_messageSequence;
    uint32_t m_lastPresenceBroadcastMs;
    uint32_t m_lastPingSweepMs;
    uint32_t m_lastPresenceUsers;
    uint32_t m_selfOriginEchoDrops;
    bool m_presenceDirty;
    RemoteNodeRecord m_remoteNodes[cfg::kMaxNeighbors];
    char m_lastMeshPacketType[24];
    char m_lastMeshFrom[24];
    char m_lastMeshTo[24];
    char m_lastSyncFrom[24];
    char m_lastError[48];
};
