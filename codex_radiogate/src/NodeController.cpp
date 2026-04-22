#include "NodeController.h"

#include <ArduinoJson.h>
#include <string.h>

#include "utils.h"

namespace {

String serializeDocument(JsonDocument& document) {
    String payload;
    serializeJson(document, payload);
    return payload;
}

const char* wsSendResultText(WebSocketGateway::SendResult result) {
    switch (result) {
        case WebSocketGateway::SendResult::Sent:
            return "sent";
        case WebSocketGateway::SendResult::Busy:
            return "busy";
        case WebSocketGateway::SendResult::Missing:
        default:
            return "missing";
    }
}

const char* meshKindText(meshproto::Kind kind) {
    switch (kind) {
        case meshproto::Kind::PresenceSnapshot:
            return "PresenceSnapshot";
        case meshproto::Kind::ChatEnvelope:
            return "ChatEnvelope";
        case meshproto::Kind::DeliveryAck:
            return "DeliveryAck";
        case meshproto::Kind::DeliveryFail:
            return "DeliveryFail";
        default:
            return "Unknown";
    }
}

bool sameRemoteState(const ClientRecord& before, const ClientRecord& after) {
    return before.isOnline == after.isOnline &&
           before.homeNodeHash == after.homeNodeHash &&
           strcmp(before.homeNodeId, after.homeNodeId) == 0 &&
           strcmp(before.displayNode, after.displayNode) == 0;
}

}  // namespace

NodeController::NodeController()
    : m_httpApi(*this),
      m_webSocket(*this),
      m_monotonicNowMs(0),
      m_messageSequence(0),
      m_lastPresenceBroadcastMs(0),
      m_lastPingSweepMs(0),
      m_lastPresenceUsers(0),
      m_selfOriginEchoDrops(0),
      m_presenceDirty(false),
      m_remoteNodes{},
      m_lastMeshPacketType{},
      m_lastMeshFrom{},
      m_lastMeshTo{},
      m_lastSyncFrom{},
      m_lastError{} {
    utils::safeCopy(m_lastMeshPacketType, sizeof(m_lastMeshPacketType), "-");
    utils::safeCopy(m_lastMeshFrom, sizeof(m_lastMeshFrom), "-");
    utils::safeCopy(m_lastMeshTo, sizeof(m_lastMeshTo), "-");
    utils::safeCopy(m_lastSyncFrom, sizeof(m_lastSyncFrom), "-");
    utils::safeCopy(m_lastError, sizeof(m_lastError), "none");
}

bool NodeController::begin() {
    Serial.begin(115200);
    delay(80);
    randomSeed(esp_random());

    captureNowMs();

    if (!m_config.begin()) {
        setLastError("prefs-init-failed");
    }

    m_identity.begin();

    char displayName[cfg::kDisplayNameLength] = {};
    if (!m_naming.chooseDisplayName(displayName, sizeof(displayName))) {
        utils::safeCopy(displayName, sizeof(displayName), "Lora-1");
    }
    m_identity.setDisplayName(displayName);
    m_clientRegistry.setSelfNode(m_identity.info().nodeId, m_identity.info().displayName, m_identity.info().nodeHash);

    if (!m_wifiAp.begin(m_identity.info().displayName)) {
        setLastError("wifi-ap-failed");
    }

    if (!m_mesh.begin(m_config.radioProfile())) {
        setLastError("loramesher-init-failed");
    }

    m_messageSequence = esp_random();
    m_presenceDirty = true;

    m_httpApi.begin();
    m_webSocket.begin();

    char macText[20];
    snprintf(macText, sizeof(macText), "%012llX",
             static_cast<unsigned long long>(m_identity.info().efuseMac & 0xFFFFFFFFFFFFULL));
    Serial.printf("[BOOT] nodeId=%s hash=%08lX mac=%s display=%s freq=%lu ip=%s fw=%s tr=%s\n",
                  m_identity.info().nodeId,
                  static_cast<unsigned long>(m_identity.info().nodeHash),
                  macText,
                  m_identity.info().displayName,
                  static_cast<unsigned long>(m_config.radioProfile().frequencyHz),
                  m_wifiAp.ip().toString().c_str(),
                  cfg::kFirmwareBuildId,
                  cfg::kTransportRevision);
    return true;
}

void NodeController::loop() {
    m_wifiAp.loop();
    m_webSocket.loop();

    const uint32_t nowMs = captureNowMs();
    m_mesh.loop(nowMs);

    processMeshFrames(nowMs);
    processPendingMessages(nowMs);
    processSessionMaintenance(nowMs);
    processRemoteNodeMaintenance(nowMs);
    processPresenceBroadcast(nowMs);
}

uint32_t NodeController::captureNowMs() {
    const uint32_t nowMs = millis();
    if (m_monotonicNowMs == 0 || utils::timeAfterOrEqual(nowMs, m_monotonicNowMs)) {
        m_monotonicNowMs = nowMs;
    }
    return m_monotonicNowMs;
}

uint32_t NodeController::monotonicNowMs() const {
    return m_monotonicNowMs;
}

NodeController::ConnectReply NodeController::handleConnect(const char* username,
                                                           const char* stableClientId,
                                                           const char* mac,
                                                           uint32_t nowMs) {
    ConnectReply reply{};
    reply.httpStatus = 400;
    utils::safeCopy(reply.status, sizeof(reply.status), "error");
    utils::safeCopy(reply.code, sizeof(reply.code), "INVALID_REQUEST");
    utils::safeCopy(reply.message, sizeof(reply.message), "Invalid connect request");

    if (!utils::isAllowedUsername(username) ||
        stableClientId == nullptr || strlen(stableClientId) < 3 ||
        mac == nullptr || strlen(mac) < 3) {
        utils::safeCopy(reply.code, sizeof(reply.code), "INVALID_USERNAME");
        utils::safeCopy(reply.message, sizeof(reply.message), "Username or client identity is invalid");
        return reply;
    }

    ClientRecord* byUsername = m_clientRegistry.findByUsername(username);
    if (byUsername != nullptr && !byUsername->isLocal) {
        if (clientOnlineState(*byUsername)) {
            reply.httpStatus = 409;
            utils::safeCopy(reply.code, sizeof(reply.code), "USERNAME_TAKEN");
            utils::safeCopy(reply.message, sizeof(reply.message), "Username is already active on another node");
            return reply;
        }
        m_clientRegistry.removeRemoteByUsername(username);
        byUsername = nullptr;
    }

    ClientRecord* byStableClientId = m_clientRegistry.findByStableClientId(stableClientId);
    if (byStableClientId != nullptr && !byStableClientId->isLocal) {
        if (clientOnlineState(*byStableClientId)) {
            reply.httpStatus = 409;
            utils::safeCopy(reply.code, sizeof(reply.code), "USERNAME_TAKEN");
            utils::safeCopy(reply.message, sizeof(reply.message), "Stable client id is active on another node");
            return reply;
        }
        m_clientRegistry.removeRemoteByUsername(byStableClientId->username);
        byStableClientId = nullptr;
    }

    if (byUsername != nullptr && strcmp(byUsername->stableClientId, stableClientId) != 0) {
        reply.httpStatus = 409;
        utils::safeCopy(reply.code, sizeof(reply.code), "USERNAME_TAKEN");
        utils::safeCopy(reply.message, sizeof(reply.message), "Username is already reserved by another client");
        return reply;
    }

    if (byStableClientId != nullptr && strcmp(byStableClientId->username, username) != 0) {
        reply.httpStatus = 409;
        utils::safeCopy(reply.code, sizeof(reply.code), "USERNAME_TAKEN");
        utils::safeCopy(reply.message, sizeof(reply.message), "Stable client id belongs to another username");
        return reply;
    }

    const bool newReservation = (byUsername == nullptr && byStableClientId == nullptr);
    if (newReservation && m_clientRegistry.localReservedCount() >= cfg::kMaxLocalClients) {
        reply.httpStatus = 503;
        utils::safeCopy(reply.code, sizeof(reply.code), "NODE_FULL");
        utils::safeCopy(reply.message, sizeof(reply.message), "Node has reached local client capacity");
        return reply;
    }

    SessionRecord session{};
    if (!m_sessionManager.createOrRefresh(username, stableClientId, mac, nowMs, session)) {
        reply.httpStatus = 503;
        utils::safeCopy(reply.code, sizeof(reply.code), "SESSION_UNAVAILABLE");
        utils::safeCopy(reply.message, sizeof(reply.message), "Failed to allocate session");
        return reply;
    }

    ClientRecord* record = m_clientRegistry.upsertLocalClient(username, stableClientId, mac, nowMs);
    if (record == nullptr) {
        reply.httpStatus = 503;
        utils::safeCopy(reply.code, sizeof(reply.code), "NODE_FULL");
        utils::safeCopy(reply.message, sizeof(reply.message), "Failed to reserve local client slot");
        return reply;
    }
    m_clientRegistry.bindSession(username, utils::fnv1a32(session.token), nowMs);

    reply.httpStatus = 200;
    reply.newClient = newReservation;
    utils::safeCopy(reply.status, sizeof(reply.status), "ok");
    utils::safeCopy(reply.code, sizeof(reply.code), "");
    utils::safeCopy(reply.message, sizeof(reply.message), "");
    utils::safeCopy(reply.username, sizeof(reply.username), username);
    utils::safeCopy(reply.sessionToken, sizeof(reply.sessionToken), session.token);
    return reply;
}

uint16_t NodeController::buildClientsResponse(const char* username,
                                              const char* token,
                                              JsonDocument& document,
                                              uint32_t nowMs) {
    if (m_sessionManager.validate(username, token, nowMs) == nullptr) {
        document["status"] = "error";
        document["code"] = "INVALID_SESSION";
        document["message"] = "Session token is invalid";
        return 401;
    }

    document["status"] = "ok";
    JsonArray clients = document["clients"].to<JsonArray>();
    const ClientRecord* records = m_clientRegistry.records();
    for (size_t index = 0; index < cfg::kMaxKnownClients; ++index) {
        const ClientRecord& record = records[index];
        if (!record.used) {
            continue;
        }
        JsonObject entry = clients.add<JsonObject>();
        entry["username"] = record.username;
        entry["homeNode"] = record.homeNodeId;
        entry["displayNode"] = record.displayNode;
        entry["isOnline"] = clientOnlineState(record);
        entry["isLocal"] = record.isLocal;
        entry["homeNodeHash"] = static_cast<uint32_t>(record.homeNodeHash);
        entry["scope"] = record.isLocal ? "LOCAL" : "MESH";
    }
    return 200;
}

NodeController::SendReply NodeController::handleSend(const char* sessionToken,
                                                     const char* toUsername,
                                                     const char* clientMessageId,
                                                     const char* content,
                                                     uint32_t nowMs) {
    SendReply reply{};
    reply.httpStatus = 400;
    utils::safeCopy(reply.status, sizeof(reply.status), "error");
    utils::safeCopy(reply.code, sizeof(reply.code), "INVALID_REQUEST");
    utils::safeCopy(reply.message, sizeof(reply.message), "Invalid send request");

    SessionRecord* session = m_sessionManager.validateByToken(sessionToken, nowMs);
    if (session == nullptr) {
        reply.httpStatus = 401;
        utils::safeCopy(reply.code, sizeof(reply.code), "INVALID_SESSION");
        utils::safeCopy(reply.message, sizeof(reply.message), "Session token is invalid");
        return reply;
    }

    if (toUsername == nullptr || toUsername[0] == '\0' || content == nullptr || content[0] == '\0') {
        utils::safeCopy(reply.code, sizeof(reply.code), "INVALID_REQUEST");
        utils::safeCopy(reply.message, sizeof(reply.message), "Recipient and message content are required");
        return reply;
    }
    const size_t contentLength = strlen(content);
    if (contentLength > cfg::kMaxMessageText) {
        reply.httpStatus = 413;
        utils::safeCopy(reply.code, sizeof(reply.code), "PAYLOAD_TOO_LARGE");
        utils::safeCopy(reply.message, sizeof(reply.message), "Message exceeds maximum length");
        return reply;
    }
    if (m_pendingMessages.count() >= cfg::kMaxPendingMessages) {
        reply.httpStatus = 429;
        utils::safeCopy(reply.code, sizeof(reply.code), "QUEUE_FULL");
        utils::safeCopy(reply.message, sizeof(reply.message), "Pending delivery queue is full");
        return reply;
    }

    if (clientMessageId != nullptr && clientMessageId[0] != '\0') {
        PendingMessage* existing = m_pendingMessages.findByClientMessageId(clientMessageId);
        if (existing != nullptr && strcmp(existing->fromUsername, session->username) == 0 && strcmp(existing->toUsername, toUsername) == 0) {
            reply.httpStatus = 202;
            utils::safeCopy(reply.status, sizeof(reply.status), "ok");
            utils::safeCopy(reply.messageId, sizeof(reply.messageId), existing->messageId);
            return reply;
        }
    }

    ClientRecord* recipient = m_clientRegistry.findByUsername(toUsername);
    if (recipient == nullptr) {
        reply.httpStatus = 404;
        utils::safeCopy(reply.code, sizeof(reply.code), "USER_NOT_FOUND");
        utils::safeCopy(reply.message, sizeof(reply.message), "Recipient is not known to this node");
        return reply;
    }

    if (!clientOnlineState(*recipient)) {
        reply.httpStatus = 409;
        utils::safeCopy(reply.code, sizeof(reply.code), "USER_OFFLINE");
        utils::safeCopy(reply.message, sizeof(reply.message), "Recipient is offline");
        return reply;
    }

    PendingMessage pending{};
    pending.used = true;
    pending.createdAtMs = nowMs;
    pending.lastTryMs = nowMs;
    pending.retryCount = 0;
    pending.status = MessageStatus::Pending;
    utils::safeCopy(pending.fromUsername, sizeof(pending.fromUsername), session->username);
    utils::safeCopy(pending.toUsername, sizeof(pending.toUsername), toUsername);
    utils::safeCopy(pending.text, sizeof(pending.text), content);
    if (clientMessageId != nullptr) {
        utils::safeCopy(pending.clientMessageId, sizeof(pending.clientMessageId), clientMessageId);
    }
    utils::formatMessageId(pending.messageId, sizeof(pending.messageId), m_identity.info().nodeId, nextMessageSequence());

    if (recipient->isLocal) {
        const bool wasOnline = clientOnlineState(*recipient);
        const WebSocketGateway::SendResult result =
            sendIncomingMessageToUser(recipient->username, pending.messageId, session->username, content, nowMs);
        if (result == WebSocketGateway::SendResult::Busy) {
            reply.httpStatus = 429;
            utils::safeCopy(reply.code, sizeof(reply.code), "QUEUE_FULL");
            utils::safeCopy(reply.message, sizeof(reply.message), "Recipient WebSocket queue is full");
            return reply;
        }
        if (result == WebSocketGateway::SendResult::Missing) {
            m_clientRegistry.markOfflineByUsername(recipient->username, nowMs);
            if (wasOnline) {
                if (ClientRecord* updated = m_clientRegistry.findByUsername(recipient->username); updated != nullptr) {
                    sendNetworkEvent("USER_LEFT", *updated, false);
                }
                m_presenceDirty = true;
            }
            reply.httpStatus = 409;
            utils::safeCopy(reply.code, sizeof(reply.code), "USER_OFFLINE");
            utils::safeCopy(reply.message, sizeof(reply.message), "Recipient WebSocket is unavailable");
            return reply;
        }

        pending.awaitingLocalWsAck = true;
        pending.awaitingDeliveryAck = false;
        pending.awaitingNodeAck = false;
        if (!m_pendingMessages.add(pending)) {
            reply.httpStatus = 429;
            utils::safeCopy(reply.code, sizeof(reply.code), "QUEUE_FULL");
            utils::safeCopy(reply.message, sizeof(reply.message), "Pending delivery queue is full");
            return reply;
        }
    } else {
        if (contentLength > cfg::kMaxMeshMessageText) {
            reply.httpStatus = 413;
            utils::safeCopy(reply.code, sizeof(reply.code), "PAYLOAD_TOO_LARGE");
            utils::safeCopy(reply.message, sizeof(reply.message), "Remote mesh message exceeds single-packet limit");
            return reply;
        }

        const RemoteNodeRecord* remoteNode = findRemoteNodeByHash(recipient->homeNodeHash);
        if (remoteNode == nullptr || remoteNode->meshAddress == 0) {
            reply.httpStatus = 409;
            utils::safeCopy(reply.code, sizeof(reply.code), "USER_OFFLINE");
            utils::safeCopy(reply.message, sizeof(reply.message), "Remote user has no reachable home node");
            return reply;
        }

        meshproto::ChatEnvelope envelope{};
        envelope.senderMeshAddress = m_mesh.localAddress();
        envelope.senderNodeHash = m_identity.info().nodeHash;
        envelope.recipientNodeHash = recipient->homeNodeHash;
        envelope.timestampMs = nowMs;
        utils::safeCopy(envelope.fromUsername, sizeof(envelope.fromUsername), session->username);
        utils::safeCopy(envelope.toUsername, sizeof(envelope.toUsername), toUsername);
        utils::safeCopy(envelope.messageId, sizeof(envelope.messageId), pending.messageId);
        utils::safeCopy(envelope.content, sizeof(envelope.content), content);

        uint8_t buffer[cfg::kMaxMeshAppPayload] = {};
        size_t writtenSize = 0;
        if (!meshproto::encodeChatEnvelope(envelope, buffer, sizeof(buffer), writtenSize)) {
            reply.httpStatus = 413;
            utils::safeCopy(reply.code, sizeof(reply.code), "PAYLOAD_TOO_LARGE");
            utils::safeCopy(reply.message, sizeof(reply.message), "Remote mesh payload does not fit in one packet");
            return reply;
        }

        const bool sendOk = m_mesh.sendReliable(remoteNode->meshAddress, buffer, writtenSize, nowMs);
        Serial.printf("[CHAT] send msg=%s from=%s to=%s dstNode=%08lX meshDst=%04X bytes=%u result=%s\n",
                      pending.messageId,
                      session->username,
                      toUsername,
                      static_cast<unsigned long>(recipient->homeNodeHash),
                      static_cast<unsigned>(remoteNode->meshAddress),
                      static_cast<unsigned>(writtenSize),
                      sendOk ? "queued" : "route-miss");
        if (!sendOk) {
            reply.httpStatus = 503;
            utils::safeCopy(reply.code, sizeof(reply.code), "ROUTE_NOT_FOUND");
            utils::safeCopy(reply.message, sizeof(reply.message), "Remote home node is not reachable");
            return reply;
        }

        pending.dstNodeHash = recipient->homeNodeHash;
        pending.originNodeHash = m_identity.info().nodeHash;
        pending.awaitingDeliveryAck = true;
        pending.awaitingLocalWsAck = false;
        pending.awaitingNodeAck = false;
        if (!m_pendingMessages.add(pending)) {
            reply.httpStatus = 429;
            utils::safeCopy(reply.code, sizeof(reply.code), "QUEUE_FULL");
            utils::safeCopy(reply.message, sizeof(reply.message), "Pending delivery queue is full");
            return reply;
        }
        rememberMeshActivity("ChatEnvelope", m_identity.info().nodeId, recipient->homeNodeId);
    }

    reply.httpStatus = 202;
    utils::safeCopy(reply.status, sizeof(reply.status), "ok");
    utils::safeCopy(reply.messageId, sizeof(reply.messageId), pending.messageId);
    return reply;
}

NodeController::DisconnectReply NodeController::handleDisconnect(const char* sessionToken, uint32_t) {
    DisconnectReply reply{};
    reply.httpStatus = 400;
    utils::safeCopy(reply.status, sizeof(reply.status), "error");
    utils::safeCopy(reply.code, sizeof(reply.code), "INVALID_REQUEST");
    utils::safeCopy(reply.message, sizeof(reply.message), "Invalid disconnect request");

    SessionRecord* session = m_sessionManager.findByToken(sessionToken);
    if (session == nullptr) {
        reply.httpStatus = 401;
        utils::safeCopy(reply.code, sizeof(reply.code), "INVALID_SESSION");
        utils::safeCopy(reply.message, sizeof(reply.message), "Session token is invalid");
        return reply;
    }

    char username[cfg::kUsernameLength] = {};
    utils::safeCopy(username, sizeof(username), session->username);
    if (session->wsConnected) {
        m_webSocket.closeClient(session->wsClientId, 1000, "disconnect");
    }
    m_sessionManager.removeByToken(sessionToken, username, sizeof(username));

    ClientRecord* record = m_clientRegistry.findByUsername(username);
    if (record != nullptr && record->isLocal) {
        const bool wasOnline = clientOnlineState(*record);
        *record = ClientRecord{};
        if (wasOnline) {
            ClientRecord ghost{};
            ghost.used = true;
            ghost.isLocal = true;
            utils::safeCopy(ghost.username, sizeof(ghost.username), username);
            utils::safeCopy(ghost.homeNodeId, sizeof(ghost.homeNodeId), m_identity.info().nodeId);
            utils::safeCopy(ghost.displayNode, sizeof(ghost.displayNode), m_identity.info().displayName);
            sendNetworkEvent("USER_LEFT", ghost, false);
        }
        m_presenceDirty = true;
    }

    reply.httpStatus = 200;
    utils::safeCopy(reply.status, sizeof(reply.status), "ok");
    utils::safeCopy(reply.username, sizeof(reply.username), username);
    return reply;
}

void NodeController::fillStatus(JsonDocument& document, uint32_t nowMs) const {
    const MeshService::Stats& meshStats = m_mesh.stats();
    const PersistentConfig::RadioProfile& radioProfile = m_config.radioProfile();

    char nodeHashText[16];
    char efuseMacText[20];
    snprintf(nodeHashText, sizeof(nodeHashText), "%08lX", static_cast<unsigned long>(m_identity.info().nodeHash));
    snprintf(efuseMacText, sizeof(efuseMacText), "%012llX",
             static_cast<unsigned long long>(m_identity.info().efuseMac & 0xFFFFFFFFFFFFULL));

    document["status"] = "ok";
    document["nodeId"] = m_identity.info().nodeId;
    document["nodeHash"] = nodeHashText;
    document["efuseMacSuffix"] = efuseMacText;
    document["displayName"] = m_identity.info().displayName;
    document["uptimeMs"] = nowMs;
    document["localClients"] = static_cast<uint32_t>(countLocalReachableClients());
    document["knownClients"] = static_cast<uint32_t>(m_clientRegistry.totalCount());
    document["remoteClients"] = static_cast<uint32_t>(countRemoteClients());
    document["neighbors"] = static_cast<uint32_t>(m_mesh.neighborCount());
    document["routes"] = static_cast<uint32_t>(m_mesh.routeCount());
    document["wsClients"] = static_cast<uint32_t>(m_sessionManager.connectedCount());
    document["frequencyHz"] = static_cast<uint32_t>(radioProfile.frequencyHz);
    document["bandwidthHz"] = static_cast<uint32_t>(radioProfile.bandwidthHz);
    document["spreadingFactor"] = static_cast<uint32_t>(radioProfile.spreadingFactor);
    document["codingRate"] = static_cast<uint32_t>(radioProfile.codingRate);
    document["txPower"] = static_cast<uint32_t>(radioProfile.txPower);
    document["syncWord"] = static_cast<uint32_t>(radioProfile.syncWord);
    document["freeHeap"] = static_cast<uint32_t>(ESP.getFreeHeap());
    document["minFreeHeap"] = static_cast<uint32_t>(ESP.getMinFreeHeap());
    document["freePsram"] = static_cast<uint32_t>(ESP.getFreePsram());
    document["loraQueue"] = static_cast<uint32_t>(m_mesh.queueDepth());
    document["meshQueueDepth"] = static_cast<uint32_t>(m_mesh.queueDepth());
    document["presenceIntervalMs"] = static_cast<uint32_t>(cfg::kPresenceIntervalMs);
    document["seenPackets"] = 0;
    document["wsAwaitingPong"] = static_cast<uint32_t>(m_sessionManager.awaitingPongCount());
    document["lastWsPingMs"] = static_cast<uint32_t>(m_sessionManager.latestPingSentMs());
    document["lastWsPongMs"] = static_cast<uint32_t>(m_sessionManager.latestPongMs());
    document["meshAddress"] = static_cast<uint32_t>(m_mesh.localAddress());
    document["loraTxCount"] = static_cast<uint32_t>(meshStats.txCount);
    document["loraRxCount"] = static_cast<uint32_t>(meshStats.rxCount);
    document["lastLoRaRssi"] = static_cast<int32_t>(meshStats.lastRssi);
    document["lastLoRaSnr"] = static_cast<double>(meshStats.lastSnr);
    document["lastRadioActivityMs"] = static_cast<uint32_t>(meshStats.lastActivityMs);
    document["lastRadioError"] = meshStats.lastError;
    document["lastMeshPacketType"] = m_lastMeshPacketType;
    document["lastMeshFrom"] = m_lastMeshFrom;
    document["lastMeshTo"] = m_lastMeshTo;
    document["lastPresenceUsers"] = m_lastPresenceUsers;
    document["lastPresenceFrom"] = m_lastSyncFrom;
    document["lastSyncClientCount"] = m_lastPresenceUsers;
    document["lastSyncFrom"] = m_lastSyncFrom;
    document["selfOriginEchoDrops"] = m_selfOriginEchoDrops;
    document["firmwareBuildId"] = cfg::kFirmwareBuildId;
    document["transportRevision"] = cfg::kTransportRevision;
    document["lastError"] = m_lastError;
}

bool NodeController::authorizeWebSocket(const char* username,
                                        const char* token,
                                        uint32_t clientId,
                                        uint32_t nowMs) {
    ClientRecord* record = m_clientRegistry.findByUsername(username);
    if (record == nullptr || !record->isLocal) {
        Serial.printf("[WS] reject user=%s ws=%lu reason=session-missing token=- expected=- now=%lu total=%u local=%u\n",
                      username,
                      static_cast<unsigned long>(clientId),
                      static_cast<unsigned long>(nowMs),
                      static_cast<unsigned>(m_sessionManager.connectedCount()),
                      static_cast<unsigned>(countLocalReachableClients()));
        return false;
    }

    const bool wasOnline = clientOnlineState(*record);
    SessionRecord* session = m_sessionManager.attachWs(username, token, clientId, nowMs);
    if (session == nullptr) {
        Serial.printf("[WS] reject user=%s ws=%lu reason=token-mismatch token=%s expected=%08lX now=%lu total=%u local=%u\n",
                      username,
                      static_cast<unsigned long>(clientId),
                      token != nullptr ? token : "-",
                      static_cast<unsigned long>(record->sessionHash),
                      static_cast<unsigned long>(nowMs),
                      static_cast<unsigned>(m_sessionManager.connectedCount()),
                      static_cast<unsigned>(countLocalReachableClients()));
        return false;
    }

    const uint32_t sessionHash = utils::fnv1a32(session->token);
    m_clientRegistry.markOnline(username, sessionHash, nowMs);

    Serial.printf("[WS] attach user=%s ws=%lu was=%u token=%.8s now=%lu attached=%lu lastAct=%lu lastPing=%lu lastPong=%lu awaiting=%u total=%u local=%u\n",
                  username,
                  static_cast<unsigned long>(clientId),
                  wasOnline ? 1U : 0U,
                  session->token,
                  static_cast<unsigned long>(nowMs),
                  static_cast<unsigned long>(session->attachedAtMs),
                  static_cast<unsigned long>(session->lastActivityMs),
                  static_cast<unsigned long>(session->lastPingSentMs),
                  static_cast<unsigned long>(session->lastPongMs),
                  session->awaitingPong ? 1U : 0U,
                  static_cast<unsigned>(m_sessionManager.connectedCount()),
                  static_cast<unsigned>(countLocalReachableClients()));

    if (!wasOnline) {
        sendNetworkEvent("USER_JOINED", *record, true);
        m_presenceDirty = true;
    }
    return true;
}

void NodeController::handleWebSocketPong(uint32_t clientId, uint32_t nowMs) {
    SessionRecord* session = m_sessionManager.findByWsClientId(clientId);
    if (session == nullptr) {
        return;
    }

    m_sessionManager.markPong(clientId, nowMs);
    session = m_sessionManager.findByWsClientId(clientId);
    if (session == nullptr) {
        return;
    }

    Serial.printf("[WS] pong user=%s ws=%lu token=%.8s now=%lu attached=%lu lastAct=%lu lastPing=%lu lastPong=%lu awaiting=%u total=%u local=%u\n",
                  session->username,
                  static_cast<unsigned long>(clientId),
                  session->token,
                  static_cast<unsigned long>(nowMs),
                  static_cast<unsigned long>(session->attachedAtMs),
                  static_cast<unsigned long>(session->lastActivityMs),
                  static_cast<unsigned long>(session->lastPingSentMs),
                  static_cast<unsigned long>(session->lastPongMs),
                  session->awaitingPong ? 1U : 0U,
                  static_cast<unsigned>(m_sessionManager.connectedCount()),
                  static_cast<unsigned>(countLocalReachableClients()));
}

void NodeController::handleWebSocketText(uint32_t clientId, const char* payload, size_t length, uint32_t nowMs) {
    SessionRecord* session = m_sessionManager.findByWsClientId(clientId);
    if (session == nullptr || payload == nullptr || length == 0) {
        return;
    }
    session->lastActivityMs = nowMs;

    JsonDocument document;
    if (deserializeJson(document, payload, length) != DeserializationError::Ok) {
        return;
    }

    const String type = document["type"] | "";
    if (type == "PONG") {
        handleWebSocketPong(clientId, nowMs);
        return;
    }
    if (type != "MESSAGE_ACK") {
        return;
    }

    const String messageId = document["messageId"] | "";
    if (messageId.isEmpty()) {
        return;
    }

    PendingMessage* pending = m_pendingMessages.findByMessageId(messageId.c_str());
    if (pending == nullptr || strcmp(pending->toUsername, session->username) != 0) {
        return;
    }

    if (pending->originMeshAddress != 0) {
        sendDeliveryAck(static_cast<uint16_t>(pending->originMeshAddress), pending->messageId, nowMs);
    } else {
        sendMessageStatusToUser(pending->fromUsername, pending->messageId, "delivered", nullptr);
    }
    m_pendingMessages.markDelivered(pending->messageId);
}

void NodeController::handleWebSocketDisconnect(uint32_t clientId, uint32_t nowMs) {
    SessionRecord* session = m_sessionManager.findByWsClientId(clientId);
    if (session == nullptr) {
        Serial.printf("[WS] detach-miss ws=%lu total=%u local=%u\n",
                      static_cast<unsigned long>(clientId),
                      static_cast<unsigned>(m_sessionManager.connectedCount()),
                      static_cast<unsigned>(countLocalReachableClients()));
        return;
    }

    char username[cfg::kUsernameLength] = {};
    char tokenPrefix[9] = {};
    utils::safeCopy(username, sizeof(username), session->username);
    for (size_t index = 0; index < 8 && session->token[index] != '\0'; ++index) {
        tokenPrefix[index] = session->token[index];
        tokenPrefix[index + 1] = '\0';
    }

    ClientRecord* record = m_clientRegistry.findByUsername(username);
    const bool wasOnline = record != nullptr && clientOnlineState(*record);
    m_sessionManager.detachWs(clientId);
    m_clientRegistry.markOfflineByUsername(username, nowMs);

    Serial.printf("[WS] detach user=%s ws=%lu was=%u token=%s total=%u local=%u\n",
                  username,
                  static_cast<unsigned long>(clientId),
                  wasOnline ? 1U : 0U,
                  tokenPrefix[0] != '\0' ? tokenPrefix : "-",
                  static_cast<unsigned>(m_sessionManager.connectedCount()),
                  static_cast<unsigned>(countLocalReachableClients()));

    if (record != nullptr && wasOnline) {
        sendNetworkEvent("USER_LEFT", *record, false);
        m_presenceDirty = true;
    }
}

void NodeController::processMeshFrames(uint32_t nowMs) {
    MeshReceivedFrame frame{};
    while (m_mesh.popReceived(frame)) {
        handleMeshFrame(frame, nowMs);
    }
}

void NodeController::processPresenceBroadcast(uint32_t nowMs) {
    bool clamped = false;
    if (m_presenceDirty || utils::elapsedReached(nowMs, m_lastPresenceBroadcastMs, cfg::kPresenceIntervalMs, &clamped)) {
        if (broadcastPresenceSnapshot(nowMs)) {
            m_lastPresenceBroadcastMs = nowMs;
            m_presenceDirty = false;
        }
    }
}

void NodeController::processRemoteNodeMaintenance(uint32_t nowMs) {
    for (RemoteNodeRecord& remoteNode : m_remoteNodes) {
        if (!remoteNode.used) {
            continue;
        }
        if (!utils::elapsedReached(nowMs, remoteNode.lastPresenceMs, cfg::kPresenceTimeoutMs)) {
            continue;
        }

        ClientRecord* records = m_clientRegistry.records();
        for (size_t index = 0; index < cfg::kMaxKnownClients; ++index) {
            ClientRecord& record = records[index];
            if (!record.used || record.isLocal || record.homeNodeHash != remoteNode.nodeHash) {
                continue;
            }
            if (record.isOnline) {
                m_clientRegistry.markOfflineByUsername(record.username, nowMs);
                if (ClientRecord* updated = m_clientRegistry.findByUsername(record.username); updated != nullptr) {
                    sendNetworkEvent("USER_LEFT", *updated, false);
                }
            }
        }
        remoteNode = RemoteNodeRecord{};
    }
}

void NodeController::processPendingMessages(uint32_t nowMs) {
    PendingMessage* records = m_pendingMessages.records();
    for (size_t index = 0; index < cfg::kMaxPendingMessages; ++index) {
        PendingMessage& record = records[index];
        if (!record.used) {
            continue;
        }

        if (record.status == MessageStatus::Delivered || record.status == MessageStatus::Failed) {
            record = PendingMessage{};
            continue;
        }

        if (record.awaitingLocalWsAck &&
            utils::elapsedReached(nowMs, record.createdAtMs, cfg::kLocalDeliveryAckTimeoutMs)) {
            if (record.originMeshAddress != 0) {
                sendDeliveryFail(static_cast<uint16_t>(record.originMeshAddress), record.messageId, DeliveryFailure::Timeout, nowMs);
            } else {
                sendMessageStatusToUser(record.fromUsername, record.messageId, "failed", "TIMEOUT");
            }
            m_pendingMessages.markFailed(record.messageId);
            record = PendingMessage{};
            continue;
        }

        if (record.awaitingDeliveryAck &&
            utils::elapsedReached(nowMs, record.createdAtMs, cfg::kRemoteDeliveryTimeoutMs)) {
            sendMessageStatusToUser(record.fromUsername, record.messageId, "failed", "TIMEOUT");
            m_pendingMessages.markFailed(record.messageId);
            record = PendingMessage{};
        }
    }
}

void NodeController::processSessionMaintenance(uint32_t nowMs) {
    SessionManager::ExpiredSession expired[cfg::kMaxSessions] = {};
    const size_t expiredCount = m_sessionManager.cleanup(nowMs, expired, cfg::kMaxSessions);
    for (size_t index = 0; index < expiredCount; ++index) {
        const SessionManager::ExpiredSession& session = expired[index];
        Serial.printf("[WS] expire user=%s ws=%lu reason=%s token=%s now=%lu attached=%lu lastAct=%lu lastPing=%lu lastPong=%lu awaiting=%u idleDelta=%lu pongDelta=%lu clamped=%u had=%u total=%u local=%u\n",
                      session.username,
                      static_cast<unsigned long>(session.wsClientId),
                      session.reason == SessionManager::ExpireReason::PongTimeout ? "pong-timeout" : "idle",
                      session.tokenPrefix,
                      static_cast<unsigned long>(nowMs),
                      static_cast<unsigned long>(session.attachedAtMs),
                      static_cast<unsigned long>(session.lastActivityMs),
                      static_cast<unsigned long>(session.lastPingSentMs),
                      static_cast<unsigned long>(session.lastPongMs),
                      session.awaitingPong ? 1U : 0U,
                      static_cast<unsigned long>(session.idleDeltaMs),
                      static_cast<unsigned long>(session.pongDeltaMs),
                      session.futureClamped ? 1U : 0U,
                      session.wasWsConnected ? 1U : 0U,
                      static_cast<unsigned>(m_sessionManager.connectedCount()),
                      static_cast<unsigned>(countLocalReachableClients()));

        ClientRecord* record = m_clientRegistry.findByUsername(session.username);
        const bool wasOnline = record != nullptr && clientOnlineState(*record);
        if (record != nullptr && record->isLocal) {
            *record = ClientRecord{};
            if (wasOnline) {
                ClientRecord ghost{};
                ghost.used = true;
                ghost.isLocal = true;
                utils::safeCopy(ghost.username, sizeof(ghost.username), session.username);
                utils::safeCopy(ghost.homeNodeId, sizeof(ghost.homeNodeId), m_identity.info().nodeId);
                utils::safeCopy(ghost.displayNode, sizeof(ghost.displayNode), m_identity.info().displayName);
                sendNetworkEvent("USER_LEFT", ghost, false);
            }
            m_presenceDirty = true;
        }
        if (session.wasWsConnected && session.wsClientId != 0) {
            m_webSocket.closeClient(session.wsClientId, 1001, "session-expired");
        }
    }

    if (!utils::elapsedReached(nowMs, m_lastPingSweepMs, cfg::kWsPingIntervalMs)) {
        return;
    }
    m_lastPingSweepMs = nowMs;

    SessionRecord* sessions = m_sessionManager.records();
    for (size_t index = 0; index < cfg::kMaxSessions; ++index) {
        SessionRecord& session = sessions[index];
        if (!session.used || !session.wsConnected) {
            continue;
        }

        const WebSocketGateway::SendResult result = m_webSocket.pingClient(session.wsClientId);
        if (result == WebSocketGateway::SendResult::Sent) {
            m_sessionManager.markPingSent(session.wsClientId, nowMs);
        } else if (result == WebSocketGateway::SendResult::Missing) {
            handleWebSocketDisconnect(session.wsClientId, nowMs);
        }

        SessionRecord* updated = m_sessionManager.findByUsername(session.username);
        const SessionRecord* active = updated != nullptr ? updated : &session;
        Serial.printf("[WS] ping user=%s ws=%lu token=%.8s result=%s now=%lu attached=%lu lastAct=%lu lastPing=%lu lastPong=%lu awaiting=%u total=%u local=%u\n",
                      active->username,
                      static_cast<unsigned long>(active->wsClientId),
                      active->token,
                      wsSendResultText(result),
                      static_cast<unsigned long>(nowMs),
                      static_cast<unsigned long>(active->attachedAtMs),
                      static_cast<unsigned long>(active->lastActivityMs),
                      static_cast<unsigned long>(active->lastPingSentMs),
                      static_cast<unsigned long>(active->lastPongMs),
                      active->awaitingPong ? 1U : 0U,
                      static_cast<unsigned>(m_sessionManager.connectedCount()),
                      static_cast<unsigned>(countLocalReachableClients()));
    }
}

void NodeController::handleMeshFrame(const MeshReceivedFrame& frame, uint32_t nowMs) {
    bool valid = false;
    const meshproto::Kind kind = meshproto::peekKind(frame.payload, frame.payloadLength, valid);
    if (!valid) {
        return;
    }

    char fromText[24];
    char toText[24];
    snprintf(fromText, sizeof(fromText), "%04X", static_cast<unsigned>(frame.srcAddress));
    snprintf(toText, sizeof(toText), "%04X", static_cast<unsigned>(frame.dstAddress));
    rememberMeshActivity(meshKindText(kind), fromText, toText);

    Serial.printf("[LM-RX] type=%s src=%04X dst=%04X len=%u\n",
                  meshKindText(kind),
                  static_cast<unsigned>(frame.srcAddress),
                  static_cast<unsigned>(frame.dstAddress),
                  static_cast<unsigned>(frame.payloadLength));

    switch (kind) {
        case meshproto::Kind::PresenceSnapshot: {
            meshproto::PresenceSnapshot snapshot{};
            if (meshproto::decodePresenceSnapshot(frame.payload, frame.payloadLength, snapshot)) {
                handlePresenceSnapshot(snapshot, frame.srcAddress, nowMs);
            }
            break;
        }
        case meshproto::Kind::ChatEnvelope: {
            meshproto::ChatEnvelope envelope{};
            if (meshproto::decodeChatEnvelope(frame.payload, frame.payloadLength, envelope)) {
                handleChatEnvelope(envelope, frame.srcAddress, nowMs);
            }
            break;
        }
        case meshproto::Kind::DeliveryAck: {
            meshproto::DeliveryAck ack{};
            if (meshproto::decodeDeliveryAck(frame.payload, frame.payloadLength, ack)) {
                handleDeliveryAck(ack, frame.srcAddress, nowMs);
            }
            break;
        }
        case meshproto::Kind::DeliveryFail: {
            meshproto::DeliveryFail fail{};
            if (meshproto::decodeDeliveryFail(frame.payload, frame.payloadLength, fail)) {
                handleDeliveryFail(fail, frame.srcAddress, nowMs);
            }
            break;
        }
        default:
            break;
    }
}

void NodeController::handlePresenceSnapshot(const meshproto::PresenceSnapshot& snapshot, uint16_t srcAddress, uint32_t nowMs) {
    if (snapshot.nodeHash == m_identity.info().nodeHash) {
        ++m_selfOriginEchoDrops;
        return;
    }

    RemoteNodeRecord* remoteNode = upsertRemoteNode(snapshot.nodeHash, srcAddress, snapshot.nodeId, snapshot.displayName, nowMs);
    if (remoteNode == nullptr) {
        return;
    }
    remoteNode->lastUserCount = snapshot.userCount;
    remoteNode->lastPresenceMs = nowMs;

    m_lastPresenceUsers = snapshot.userCount;
    utils::safeCopy(m_lastSyncFrom, sizeof(m_lastSyncFrom), snapshot.nodeId);

    ClientRecord* records = m_clientRegistry.records();
    for (size_t index = 0; index < cfg::kMaxKnownClients; ++index) {
        ClientRecord& record = records[index];
        if (record.used && !record.isLocal && record.homeNodeHash == snapshot.nodeHash) {
            record.lastSyncSeenMs = 0;
        }
    }

    for (size_t index = 0; index < snapshot.userCount && index < cfg::kMaxLocalClients; ++index) {
        const char* remoteUsername = snapshot.usernames[index];
        if (remoteUsername == nullptr || remoteUsername[0] == '\0') {
            continue;
        }

        ClientRecord* existing = m_clientRegistry.findByUsername(remoteUsername);
        if (existing != nullptr && existing->isLocal) {
            continue;
        }

        ClientRecord before{};
        const bool hadExisting = existing != nullptr;
        if (hadExisting) {
            before = *existing;
        }

        char stableClientId[cfg::kClientIdLength];
        char pseudoMac[cfg::kMacLength];
        snprintf(stableClientId, sizeof(stableClientId), "R-%08lX-%s",
                 static_cast<unsigned long>(snapshot.nodeHash),
                 remoteUsername);
        snprintf(pseudoMac, sizeof(pseudoMac), "mesh-%04X", static_cast<unsigned>(snapshot.meshAddress));

        m_clientRegistry.upsertRemoteClient(remoteUsername,
                                            stableClientId,
                                            pseudoMac,
                                            snapshot.nodeHash,
                                            snapshot.nodeId,
                                            snapshot.displayName,
                                            true,
                                            nowMs,
                                            nowMs);

        ClientRecord* updated = m_clientRegistry.findByUsername(remoteUsername);
        if (updated == nullptr || updated->isLocal) {
            continue;
        }
        updated->lastSyncSeenMs = nowMs;
        updated->lastSeenMs = nowMs;

        if (!hadExisting || !sameRemoteState(before, *updated)) {
            sendNetworkEvent("USER_JOINED", *updated, true);
        }
    }

    for (size_t index = 0; index < cfg::kMaxKnownClients; ++index) {
        ClientRecord& record = records[index];
        if (!record.used || record.isLocal || record.homeNodeHash != snapshot.nodeHash) {
            continue;
        }
        if (record.lastSyncSeenMs == nowMs || !record.isOnline) {
            continue;
        }
        m_clientRegistry.markOfflineByUsername(record.username, nowMs);
        if (ClientRecord* updated = m_clientRegistry.findByUsername(record.username); updated != nullptr) {
            sendNetworkEvent("USER_LEFT", *updated, false);
        }
    }

    Serial.printf("[PRESENCE] from=%s hash=%08lX addr=%04X users=%u\n",
                  snapshot.nodeId,
                  static_cast<unsigned long>(snapshot.nodeHash),
                  static_cast<unsigned>(snapshot.meshAddress),
                  static_cast<unsigned>(snapshot.userCount));
}

void NodeController::handleChatEnvelope(const meshproto::ChatEnvelope& envelope, uint16_t srcAddress, uint32_t nowMs) {
    if (envelope.recipientNodeHash != m_identity.info().nodeHash) {
        return;
    }

    ClientRecord* recipient = m_clientRegistry.findByUsername(envelope.toUsername);
    if (recipient == nullptr || !recipient->isLocal || !clientOnlineState(*recipient)) {
        Serial.printf("[CHAT] rx msg=%s from=%s to=%s src=%04X result=offline\n",
                      envelope.messageId,
                      envelope.fromUsername,
                      envelope.toUsername,
                      static_cast<unsigned>(srcAddress));
        sendDeliveryFail(envelope.senderMeshAddress, envelope.messageId, DeliveryFailure::UserOffline, nowMs);
        return;
    }

    const WebSocketGateway::SendResult result =
        sendIncomingMessageToUser(envelope.toUsername, envelope.messageId, envelope.fromUsername, envelope.content, envelope.timestampMs);
    if (result == WebSocketGateway::SendResult::Busy) {
        Serial.printf("[CHAT] rx msg=%s from=%s to=%s src=%04X result=queue-full\n",
                      envelope.messageId,
                      envelope.fromUsername,
                      envelope.toUsername,
                      static_cast<unsigned>(srcAddress));
        sendDeliveryFail(envelope.senderMeshAddress, envelope.messageId, DeliveryFailure::QueueFull, nowMs);
        return;
    }
    if (result == WebSocketGateway::SendResult::Missing) {
        const bool wasOnline = clientOnlineState(*recipient);
        m_clientRegistry.markOfflineByUsername(envelope.toUsername, nowMs);
        if (wasOnline) {
            if (ClientRecord* updated = m_clientRegistry.findByUsername(envelope.toUsername); updated != nullptr) {
                sendNetworkEvent("USER_LEFT", *updated, false);
            }
            m_presenceDirty = true;
        }
        Serial.printf("[CHAT] rx msg=%s from=%s to=%s src=%04X result=missing\n",
                      envelope.messageId,
                      envelope.fromUsername,
                      envelope.toUsername,
                      static_cast<unsigned>(srcAddress));
        sendDeliveryFail(envelope.senderMeshAddress, envelope.messageId, DeliveryFailure::UserOffline, nowMs);
        return;
    }

    PendingMessage pending{};
    pending.used = true;
    pending.createdAtMs = nowMs;
    pending.lastTryMs = nowMs;
    pending.status = MessageStatus::Forwarded;
    pending.originMeshAddress = envelope.senderMeshAddress;
    pending.originNodeHash = envelope.senderNodeHash;
    pending.awaitingLocalWsAck = true;
    utils::safeCopy(pending.messageId, sizeof(pending.messageId), envelope.messageId);
    utils::safeCopy(pending.fromUsername, sizeof(pending.fromUsername), envelope.fromUsername);
    utils::safeCopy(pending.toUsername, sizeof(pending.toUsername), envelope.toUsername);
    utils::safeCopy(pending.text, sizeof(pending.text), envelope.content);
    m_pendingMessages.add(pending);

    Serial.printf("[CHAT] rx msg=%s from=%s to=%s src=%04X result=local-delivered\n",
                  envelope.messageId,
                  envelope.fromUsername,
                  envelope.toUsername,
                  static_cast<unsigned>(srcAddress));
}

void NodeController::handleDeliveryAck(const meshproto::DeliveryAck& ack, uint16_t srcAddress, uint32_t) {
    PendingMessage* pending = m_pendingMessages.findByMessageId(ack.messageId);
    if (pending == nullptr) {
        return;
    }

    Serial.printf("[CHAT] ack msg=%s from=%04X dstNode=%08lX localUser=%s result=delivered\n",
                  ack.messageId,
                  static_cast<unsigned>(srcAddress),
                  static_cast<unsigned long>(pending->dstNodeHash),
                  pending->fromUsername);
    sendMessageStatusToUser(pending->fromUsername, pending->messageId, "delivered", nullptr);
    m_pendingMessages.markDelivered(pending->messageId);
}

void NodeController::handleDeliveryFail(const meshproto::DeliveryFail& fail, uint16_t srcAddress, uint32_t) {
    PendingMessage* pending = m_pendingMessages.findByMessageId(fail.messageId);
    if (pending == nullptr) {
        return;
    }

    const char* reason = utils::deliveryFailureToCode(fail.failure);
    Serial.printf("[CHAT] fail msg=%s from=%04X dstNode=%08lX localUser=%s result=failed reason=%s\n",
                  fail.messageId,
                  static_cast<unsigned>(srcAddress),
                  static_cast<unsigned long>(pending->dstNodeHash),
                  pending->fromUsername,
                  reason);
    sendMessageStatusToUser(pending->fromUsername, pending->messageId, "failed", reason);
    m_pendingMessages.markFailed(pending->messageId);
}

bool NodeController::broadcastPresenceSnapshot(uint32_t nowMs) {
    meshproto::PresenceSnapshot snapshot{};
    snapshot.meshAddress = m_mesh.localAddress();
    snapshot.nodeHash = m_identity.info().nodeHash;
    utils::safeCopy(snapshot.nodeId, sizeof(snapshot.nodeId), m_identity.info().nodeId);
    utils::safeCopy(snapshot.displayName, sizeof(snapshot.displayName), m_identity.info().displayName);

    const ClientRecord* records = m_clientRegistry.records();
    for (size_t index = 0; index < cfg::kMaxKnownClients && snapshot.userCount < cfg::kMaxLocalClients; ++index) {
        const ClientRecord& record = records[index];
        if (!record.used || !record.isLocal || !clientOnlineState(record)) {
            continue;
        }
        utils::safeCopy(snapshot.usernames[snapshot.userCount], sizeof(snapshot.usernames[snapshot.userCount]), record.username);
        ++snapshot.userCount;
    }

    uint8_t buffer[cfg::kMaxMeshAppPayload] = {};
    size_t writtenSize = 0;
    if (!meshproto::encodePresenceSnapshot(snapshot, buffer, sizeof(buffer), writtenSize)) {
        setLastError("presence-encode-failed");
        return false;
    }
    if (!m_mesh.sendBroadcast(buffer, writtenSize, nowMs)) {
        setLastError("presence-send-failed");
        return false;
    }

    m_lastPresenceBroadcastMs = nowMs;
    m_lastPresenceUsers = snapshot.userCount;
    rememberMeshActivity("PresenceSnapshot", m_identity.info().nodeId, "FFFF");
    Serial.printf("[PRESENCE] tx node=%s hash=%08lX addr=%04X users=%u\n",
                  snapshot.nodeId,
                  static_cast<unsigned long>(snapshot.nodeHash),
                  static_cast<unsigned>(snapshot.meshAddress),
                  static_cast<unsigned>(snapshot.userCount));
    return true;
}

bool NodeController::sendDeliveryAck(uint16_t dstMeshAddress, const char* messageId, uint32_t nowMs) {
    meshproto::DeliveryAck ack{};
    ack.senderMeshAddress = m_mesh.localAddress();
    ack.senderNodeHash = m_identity.info().nodeHash;
    utils::safeCopy(ack.messageId, sizeof(ack.messageId), messageId);

    uint8_t buffer[cfg::kMaxMeshAppPayload] = {};
    size_t writtenSize = 0;
    if (!meshproto::encodeDeliveryAck(ack, buffer, sizeof(buffer), writtenSize)) {
        return false;
    }

    const bool ok = m_mesh.sendReliable(dstMeshAddress, buffer, writtenSize, nowMs);
    Serial.printf("[LM-TX] type=DeliveryAck dst=%04X msg=%s result=%s\n",
                  static_cast<unsigned>(dstMeshAddress),
                  messageId,
                  ok ? "ok" : "route-miss");
    if (ok) {
        rememberMeshActivity("DeliveryAck", m_identity.info().nodeId, "-");
    }
    return ok;
}

bool NodeController::sendDeliveryFail(uint16_t dstMeshAddress, const char* messageId, DeliveryFailure failure, uint32_t nowMs) {
    meshproto::DeliveryFail fail{};
    fail.senderMeshAddress = m_mesh.localAddress();
    fail.senderNodeHash = m_identity.info().nodeHash;
    fail.failure = static_cast<uint8_t>(failure);
    utils::safeCopy(fail.messageId, sizeof(fail.messageId), messageId);

    uint8_t buffer[cfg::kMaxMeshAppPayload] = {};
    size_t writtenSize = 0;
    if (!meshproto::encodeDeliveryFail(fail, buffer, sizeof(buffer), writtenSize)) {
        return false;
    }

    const bool ok = m_mesh.sendReliable(dstMeshAddress, buffer, writtenSize, nowMs);
    Serial.printf("[LM-TX] type=DeliveryFail dst=%04X msg=%s reason=%s result=%s\n",
                  static_cast<unsigned>(dstMeshAddress),
                  messageId,
                  utils::deliveryFailureToCode(static_cast<uint8_t>(failure)),
                  ok ? "ok" : "route-miss");
    if (ok) {
        rememberMeshActivity("DeliveryFail", m_identity.info().nodeId, "-");
    }
    return ok;
}

void NodeController::sendNetworkEvent(const char* event, const ClientRecord& record, bool online) {
    JsonDocument document;
    document["type"] = "NETWORK_EVENT";
    document["event"] = event;
    document["username"] = record.username;
    document["homeNode"] = record.homeNodeId;
    document["displayNode"] = record.displayNode;
    document["online"] = online;
    const String payload = serializeDocument(document);

    const SessionRecord* sessions = m_sessionManager.records();
    for (size_t index = 0; index < cfg::kMaxSessions; ++index) {
        const SessionRecord& session = sessions[index];
        if (!session.used || !session.wsConnected) {
            continue;
        }
        m_webSocket.sendToClient(session.wsClientId, payload.c_str());
    }
}

WebSocketGateway::SendResult NodeController::sendMessageStatusToUser(const char* username,
                                                                     const char* messageId,
                                                                     const char* status,
                                                                     const char* reason) {
    JsonDocument document;
    document["type"] = "MESSAGE_STATUS";
    document["messageId"] = messageId;
    document["status"] = status;
    if (reason != nullptr && reason[0] != '\0') {
        document["reason"] = reason;
    }
    const String payload = serializeDocument(document);
    return pushTextToLocalUser(username, payload.c_str(), monotonicNowMs());
}

WebSocketGateway::SendResult NodeController::sendIncomingMessageToUser(const char* username,
                                                                       const char* messageId,
                                                                       const char* fromUsername,
                                                                       const char* content,
                                                                       uint32_t timestampMs) {
    JsonDocument document;
    document["type"] = "MESSAGE_INCOMING";
    document["messageId"] = messageId;
    document["from"] = fromUsername;
    document["content"] = content;
    document["timestamp"] = timestampMs;
    const String payload = serializeDocument(document);
    return pushTextToLocalUser(username, payload.c_str(), timestampMs);
}

WebSocketGateway::SendResult NodeController::pushTextToLocalUser(const char* username, const char* payload, uint32_t) {
    ClientRecord* record = m_clientRegistry.findByUsername(username);
    if (record == nullptr || !record->isLocal || !clientOnlineState(*record)) {
        return WebSocketGateway::SendResult::Missing;
    }

    const SessionRecord* session = m_sessionManager.findByUsername(username);
    if (session == nullptr || !session->used || !session->wsConnected || session->wsClientId == 0) {
        return WebSocketGateway::SendResult::Missing;
    }

    return m_webSocket.sendToClient(session->wsClientId, payload);
}

bool NodeController::clientOnlineState(const ClientRecord& record) const {
    if (!record.used) {
        return false;
    }
    if (record.isLocal) {
        return isLocalClientReachable(record);
    }
    return record.isOnline;
}

bool NodeController::isLocalClientReachable(const ClientRecord& record) const {
    if (!record.used || !record.isLocal || !record.isOnline || record.sessionHash == 0) {
        return false;
    }
    const SessionRecord* session = m_sessionManager.findByUsername(record.username);
    if (session == nullptr || !session->used || !session->wsConnected) {
        return false;
    }
    return utils::fnv1a32(session->token) == record.sessionHash;
}

size_t NodeController::countLocalReachableClients() const {
    size_t count = 0;
    const ClientRecord* records = m_clientRegistry.records();
    for (size_t index = 0; index < cfg::kMaxKnownClients; ++index) {
        if (records[index].used && records[index].isLocal && clientOnlineState(records[index])) {
            ++count;
        }
    }
    return count;
}

size_t NodeController::countRemoteClients() const {
    size_t count = 0;
    const ClientRecord* records = m_clientRegistry.records();
    for (size_t index = 0; index < cfg::kMaxKnownClients; ++index) {
        if (records[index].used && !records[index].isLocal && records[index].isOnline) {
            ++count;
        }
    }
    return count;
}

NodeController::RemoteNodeRecord* NodeController::findRemoteNodeByHash(uint32_t nodeHash) {
    for (RemoteNodeRecord& record : m_remoteNodes) {
        if (record.used && record.nodeHash == nodeHash) {
            return &record;
        }
    }
    return nullptr;
}

const NodeController::RemoteNodeRecord* NodeController::findRemoteNodeByHash(uint32_t nodeHash) const {
    for (const RemoteNodeRecord& record : m_remoteNodes) {
        if (record.used && record.nodeHash == nodeHash) {
            return &record;
        }
    }
    return nullptr;
}

NodeController::RemoteNodeRecord* NodeController::upsertRemoteNode(uint32_t nodeHash,
                                                                   uint16_t meshAddress,
                                                                   const char* nodeId,
                                                                   const char* displayName,
                                                                   uint32_t nowMs) {
    RemoteNodeRecord* record = findRemoteNodeByHash(nodeHash);
    if (record == nullptr) {
        for (RemoteNodeRecord& slot : m_remoteNodes) {
            if (!slot.used) {
                record = &slot;
                break;
            }
        }
    }
    if (record == nullptr) {
        RemoteNodeRecord* oldest = &m_remoteNodes[0];
        for (RemoteNodeRecord& slot : m_remoteNodes) {
            if (slot.lastPresenceMs < oldest->lastPresenceMs) {
                oldest = &slot;
            }
        }
        record = oldest;
    }

    record->used = true;
    record->meshAddress = meshAddress;
    record->nodeHash = nodeHash;
    record->lastPresenceMs = nowMs;
    utils::safeCopy(record->nodeId, sizeof(record->nodeId), nodeId);
    utils::safeCopy(record->displayName, sizeof(record->displayName), displayName);
    return record;
}

void NodeController::removeRemoteUsersForNode(uint32_t nodeHash) {
    ClientRecord* records = m_clientRegistry.records();
    for (size_t index = 0; index < cfg::kMaxKnownClients; ++index) {
        ClientRecord& record = records[index];
        if (record.used && !record.isLocal && record.homeNodeHash == nodeHash) {
            record = ClientRecord{};
        }
    }
}

void NodeController::rememberMeshActivity(const char* type, const char* from, const char* to) {
    utils::safeCopy(m_lastMeshPacketType, sizeof(m_lastMeshPacketType), type);
    utils::safeCopy(m_lastMeshFrom, sizeof(m_lastMeshFrom), from);
    utils::safeCopy(m_lastMeshTo, sizeof(m_lastMeshTo), to);
}

void NodeController::setLastError(const char* errorText) {
    utils::safeCopy(m_lastError, sizeof(m_lastError), errorText);
}

uint32_t NodeController::nextMessageSequence() {
    return ++m_messageSequence;
}
