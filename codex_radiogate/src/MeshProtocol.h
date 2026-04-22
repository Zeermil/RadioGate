#pragma once

#include <Arduino.h>

#include "config.h"
#include "mesh_types.h"

namespace meshproto {

constexpr uint8_t kProtocolVersion = 1;

enum class Kind : uint8_t {
    PresenceSnapshot = 1,
    ChatEnvelope = 2,
    DeliveryAck = 3,
    DeliveryFail = 4,
};

struct PresenceSnapshot {
    uint16_t meshAddress;
    uint32_t nodeHash;
    char nodeId[cfg::kNodeIdLength];
    char displayName[cfg::kDisplayNameLength];
    uint8_t userCount;
    char usernames[cfg::kMaxLocalClients][cfg::kUsernameLength];
};

struct ChatEnvelope {
    uint16_t senderMeshAddress;
    uint32_t senderNodeHash;
    uint32_t recipientNodeHash;
    char fromUsername[cfg::kUsernameLength];
    char toUsername[cfg::kUsernameLength];
    char messageId[cfg::kMessageIdLength];
    uint32_t timestampMs;
    char content[cfg::kMaxMessageText + 1];
};

struct DeliveryAck {
    uint16_t senderMeshAddress;
    uint32_t senderNodeHash;
    char messageId[cfg::kMessageIdLength];
};

struct DeliveryFail {
    uint16_t senderMeshAddress;
    uint32_t senderNodeHash;
    uint8_t failure;
    char messageId[cfg::kMessageIdLength];
};

bool encodePresenceSnapshot(const PresenceSnapshot& snapshot, uint8_t* output, size_t outputSize, size_t& writtenSize);
bool decodePresenceSnapshot(const uint8_t* payload, size_t payloadSize, PresenceSnapshot& snapshot);

bool encodeChatEnvelope(const ChatEnvelope& envelope, uint8_t* output, size_t outputSize, size_t& writtenSize);
bool decodeChatEnvelope(const uint8_t* payload, size_t payloadSize, ChatEnvelope& envelope);

bool encodeDeliveryAck(const DeliveryAck& ack, uint8_t* output, size_t outputSize, size_t& writtenSize);
bool decodeDeliveryAck(const uint8_t* payload, size_t payloadSize, DeliveryAck& ack);

bool encodeDeliveryFail(const DeliveryFail& fail, uint8_t* output, size_t outputSize, size_t& writtenSize);
bool decodeDeliveryFail(const uint8_t* payload, size_t payloadSize, DeliveryFail& fail);

Kind peekKind(const uint8_t* payload, size_t payloadSize, bool& valid);

}  // namespace meshproto
