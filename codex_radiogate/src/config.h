#pragma once

#ifndef RADIO_PROFILE_DEFAULT
#define RADIO_PROFILE_DEFAULT 433920000UL
#endif

#define RG_STRINGIFY_INNER(x) #x
#define RG_STRINGIFY(x) RG_STRINGIFY_INNER(x)

#include <Arduino.h>

namespace cfg {

constexpr uint16_t kMeshMagic = 0xCAFE;
constexpr uint8_t kMeshVersion = 1;
constexpr uint32_t kBroadcastNodeHash = 0xFFFFFFFFUL;
constexpr uint8_t kDefaultTtl = 4;

constexpr size_t kNodeIdLength = 16;
constexpr size_t kDisplayNameLength = 16;
constexpr size_t kUsernameLength = 24;
constexpr size_t kMacLength = 18;
constexpr size_t kTokenLength = 17;
constexpr size_t kClientIdLength = 40;
constexpr size_t kMessageIdLength = 24;
constexpr size_t kClientMessageIdLength = 40;
constexpr size_t kWsTextLength = 256;

constexpr size_t kMaxKnownClients = 32;
constexpr size_t kMaxLocalClients = 4;
constexpr size_t kMaxWsClients = 4;
constexpr size_t kMaxNeighbors = 16;
constexpr size_t kMaxRoutes = 32;
constexpr size_t kMaxSeenPackets = 80;
constexpr size_t kMaxPendingMessages = 30;
constexpr size_t kMaxClaims = 8;
constexpr size_t kMaxSessions = 8;
constexpr size_t kMaxFragments = 8;
constexpr size_t kMaxReassemblySize = 1024;
constexpr size_t kMaxMessageText = 160;
constexpr size_t kMaxLoRaFrame = 220;
constexpr size_t kMaxOutgoingPackets = 24;
constexpr size_t kMaxDeferredControlTasks = 8;
constexpr size_t kMeshRxQueueSize = 12;
// LoRaMesher max over-the-air packet size. Reliable payload is smaller due to control headers.
constexpr size_t kMaxLoRaPayload = 200;
// Keep app payload below the reliable single-packet ceiling (max packet size - 12-byte control header).
constexpr size_t kMaxMeshAppPayload = 188;
constexpr size_t kMaxMeshMessageText = 96;
constexpr uint32_t kPresenceIntervalMs = 30000UL;
constexpr uint32_t kPresenceTimeoutMs = kPresenceIntervalMs * 2UL;
constexpr uint32_t kLocalDeliveryAckTimeoutMs = 20000UL;
constexpr uint32_t kRemoteDeliveryTimeoutMs = 30000UL;
constexpr uint32_t kMeshReceiveTaskStackSize = 4096UL;

constexpr uint32_t kStartRandomMinMs = 500UL;
constexpr uint32_t kStartRandomMaxMs = 3000UL;
constexpr uint32_t kNodeHelloIntervalMs = 10000UL;
constexpr uint32_t kHeartbeatIntervalMs = 30000UL;
constexpr uint32_t kNodeSyncIntervalMs = 15000UL;
constexpr uint32_t kNodeHelloJitterMs = 2200UL;
constexpr uint32_t kHeartbeatJitterMs = 3200UL;
constexpr uint32_t kNodeSyncJitterMs = 2600UL;
constexpr uint32_t kNodeTimeoutMs = 90000UL;
constexpr uint32_t kSeenPacketTtlMs = 180000UL;
constexpr uint32_t kUsernameClaimWindowMinMs = 800UL;
constexpr uint32_t kUsernameClaimWindowMaxMs = 1500UL;
constexpr uint32_t kSessionIdleTimeoutMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kClientOfflineGraceMs = 45UL * 1000UL;
constexpr uint32_t kWsPingIntervalMs = 15UL * 1000UL;
constexpr uint32_t kWsPongTimeoutMs = 5UL * 1000UL;
constexpr uint32_t kRetryDelayMs = 10000UL;
constexpr uint8_t kMaxRetry = 3;
constexpr uint32_t kMessageTtlMs = 300000UL;
constexpr uint32_t kFragmentTimeoutMs = 15000UL;
constexpr uint32_t kLoRaTxGapMs = 80UL;
constexpr uint32_t kDeferredSyncResponseMinMs = 80UL;
constexpr uint32_t kDeferredSyncResponseMaxMs = 220UL;
constexpr uint32_t kDiscoverySyncRequestMinMs = 120UL;
constexpr uint32_t kDiscoverySyncRequestMaxMs = 260UL;

constexpr uint8_t kApChannel = 6;
constexpr uint8_t kApMaxClients = 8;
constexpr bool kApHidden = false;
constexpr uint16_t kHttpPort = 80;
constexpr uint16_t kWsPort = 81;

constexpr char kTransportRevision[] = "loramesher-v2-mf";
constexpr char kFirmwareBuildId[] = __DATE__ " " __TIME__;

constexpr uint8_t kPinLoRaSck = 5;
constexpr uint8_t kPinLoRaMiso = 19;
constexpr uint8_t kPinLoRaMosi = 27;
constexpr uint8_t kPinLoRaSs = 18;
constexpr uint8_t kPinLoRaRst = 23;
constexpr uint8_t kPinLoRaDio0 = 26;
constexpr uint8_t kPinLoRaDio1 = 33;

constexpr uint32_t kLoRaBandwidth = 250000UL;
constexpr uint8_t kLoRaSpreadingFactor = 8;
constexpr uint8_t kLoRaCodingRate = 5;
constexpr uint8_t kLoRaTxPower = 17;
constexpr uint8_t kLoRaSyncWord = 0x12;

constexpr char kNodeNamePrefix[] = "Lora-";
constexpr char kPreferenceNamespace[] = "radiogate";

}  // namespace cfg
