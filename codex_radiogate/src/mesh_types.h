#pragma once

#include <Arduino.h>
#include "config.h"

enum class MeshPacketType : uint8_t {
    NodeHello = 0x01,
    NodeSyncRequest = 0x02,
    NodeSyncResponse = 0x03,
    UsernameClaim = 0x04,
    UsernameConflict = 0x05,
    ClientUpdate = 0x10,
    MessageForward = 0x20,
    MessageNodeAck = 0x21,
    MessageDeliveryAck = 0x22,
    MessageDeliveryFail = 0x23,
    NodeHeartbeat = 0x24,
    RouteRequest = 0x30,
    RouteReply = 0x31,
    Fragment = 0x40,
};

enum MeshFlags : uint8_t {
    kFlagBroadcast = 0x01,
    kFlagAckRequired = 0x02,
    kFlagFragmented = 0x04,
    kFlagEncrypted = 0x08,
};

enum class MessageStatus : uint8_t {
    Pending = 0,
    Forwarded = 1,
    Delivered = 2,
    Failed = 3,
};

enum class DeliveryFailure : uint8_t {
    None = 0,
    UserOffline = 1,
    RouteNotFound = 2,
    QueueFull = 3,
    PayloadTooLarge = 4,
    Timeout = 5,
    InvalidSession = 6,
};

enum class QueuePriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
};

struct MeshHeader {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t ttl;
    uint8_t flags;
    uint32_t originNodeHash;
    uint32_t srcNodeHash;
    uint32_t dstNodeHash;
    uint32_t packetSeq;
    uint32_t packetId;
};

struct FragmentHeader {
    uint16_t fragId;
    uint8_t fragIndex;
    uint8_t fragTotal;
    uint16_t totalSize;
    uint8_t originalType;
    uint8_t originalFlags;
};

struct ReceivedFrame {
    int16_t rssi;
    float snr;
    size_t length;
    uint8_t data[cfg::kMaxLoRaFrame];
};

struct NodeIdentityInfo {
    char nodeId[cfg::kNodeIdLength];
    char displayName[cfg::kDisplayNameLength];
    uint32_t nodeHash;
    uint64_t efuseMac;
};

struct ClientRecord {
    bool used;
    char username[cfg::kUsernameLength];
    char mac[cfg::kMacLength];
    char stableClientId[cfg::kClientIdLength];
    char homeNodeId[cfg::kNodeIdLength];
    char displayNode[cfg::kDisplayNameLength];
    uint32_t homeNodeHash;
    bool isOnline;
    bool isLocal;
    uint32_t claimTimestampMs;
    uint32_t lastSeenMs;
    uint32_t lastSyncSeenMs;
    uint32_t sessionHash;
};

struct SessionRecord {
    bool used;
    char username[cfg::kUsernameLength];
    char token[cfg::kTokenLength];
    char clientMac[cfg::kMacLength];
    char stableClientId[cfg::kClientIdLength];
    uint32_t createdAtMs;
    uint32_t lastActivityMs;
    uint32_t attachedAtMs;
    uint32_t lastPingSentMs;
    uint32_t lastPongMs;
    uint32_t wsClientId;
    bool wsConnected;
    bool awaitingPong;
};

struct NeighborRecord {
    bool used;
    char nodeId[cfg::kNodeIdLength];
    char displayName[cfg::kDisplayNameLength];
    uint32_t nodeHash;
    uint32_t lastSeenMs;
    int16_t lastRssi;
    float lastSnr;
    bool alive;
};

struct RouteRecord {
    bool used;
    uint32_t dstNodeHash;
    uint32_t nextHopHash;
    char dstNodeId[cfg::kNodeIdLength];
    char nextHopNodeId[cfg::kNodeIdLength];
    uint8_t hopCount;
    int16_t rssi;
    uint32_t updatedAtMs;
};

struct SeenPacketEntry {
    bool used;
    uint32_t originNodeHash;
    uint32_t packetId;
    uint32_t expiresAtMs;
};

struct PendingMessage {
    bool used;
    char messageId[cfg::kMessageIdLength];
    char clientMessageId[cfg::kClientMessageIdLength];
    char fromUsername[cfg::kUsernameLength];
    char toUsername[cfg::kUsernameLength];
    char text[cfg::kMaxMessageText + 1];
    uint16_t originMeshAddress;
    uint32_t dstNodeHash;
    uint32_t originNodeHash;
    uint32_t previousHopHash;
    uint32_t createdAtMs;
    uint32_t lastTryMs;
    uint8_t retryCount;
    MessageStatus status;
    bool awaitingNodeAck;
    bool awaitingDeliveryAck;
    bool awaitingLocalWsAck;
};

struct FragmentAssembly {
    bool used;
    uint32_t originNodeHash;
    uint32_t srcNodeHash;
    uint32_t dstNodeHash;
    uint16_t fragId;
    uint8_t originalType;
    uint8_t originalFlags;
    uint8_t ttl;
    uint8_t totalFragments;
    uint16_t totalSize;
    uint32_t receivedMask;
    uint32_t expiresAtMs;
    size_t receivedBytes;
    uint8_t data[cfg::kMaxReassemblySize];
};

struct OutgoingMeshPacket {
    bool used;
    uint8_t type;
    uint8_t flags;
    uint8_t ttl;
    uint8_t priority;
    uint32_t originNodeHash;
    uint32_t srcNodeHash;
    uint32_t dstNodeHash;
    uint32_t packetSeq;
    uint32_t packetId;
    size_t payloadLength;
    uint8_t payload[cfg::kMaxLoRaPayload];
};

struct UsernameClaimRecord {
    bool used;
    bool conflict;
    char username[cfg::kUsernameLength];
    char mac[cfg::kMacLength];
    char stableClientId[cfg::kClientIdLength];
    uint32_t createdAtMs;
};

struct PendingClaimResult {
    bool exists;
    bool approved;
    bool conflict;
    uint32_t retryAfterMs;
};
