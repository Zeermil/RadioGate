#pragma once

#include <Arduino.h>

#include "mesh_types.h"

class ClientRegistry {
public:
    enum class ConnectDecision : uint8_t {
        Ok = 0,
        Pending = 1,
        Conflict = 2,
        Invalid = 3,
        NodeFull = 4,
    };

    struct ConnectCheck {
        ConnectDecision decision;
        uint32_t retryAfterMs;
        bool broadcastClaim;
        bool existingClient;
    };

    ClientRegistry();

    void setSelfNode(const char* nodeId, const char* displayName, uint32_t nodeHash);
    ConnectCheck beginOrResumeClaim(const char* username,
                                    const char* stableClientId,
                                    const char* mac,
                                    uint32_t nowMs);
    bool finalizeClaim(const char* username,
                       const char* stableClientId,
                       const char* mac,
                       uint32_t nowMs,
                       ClientRecord& outRecord);
    void markClaimConflict(const char* username, const char* stableClientId);
    bool shouldRejectRemoteClaim(const char* username,
                                 const char* stableClientId,
                                 uint32_t claimTimestampMs,
                                 uint32_t remoteNodeHash) const;

    void upsertRemoteClient(const char* username,
                            const char* stableClientId,
                            const char* mac,
                            uint32_t homeNodeHash,
                            const char* homeNodeId,
                            const char* displayNode,
                            bool isOnline,
                            uint32_t claimTimestampMs,
                            uint32_t nowMs);
    void bindSession(const char* username, uint32_t sessionHash, uint32_t nowMs);
    void markOfflineByUsername(const char* username, uint32_t nowMs);
    void markOfflineByNode(uint32_t homeNodeHash, uint32_t nowMs);
    void markOnline(const char* username, uint32_t sessionHash, uint32_t nowMs);
    void clearSessionHash(const char* username);
    void cleanupClaims(uint32_t nowMs);
    ClientRecord* upsertLocalClient(const char* username,
                                    const char* stableClientId,
                                    const char* mac,
                                    uint32_t nowMs);
    void removeRemoteByUsername(const char* username);
    void removeRemoteByNode(uint32_t homeNodeHash);

    ClientRecord* findByUsername(const char* username);
    ClientRecord* findByStableClientId(const char* stableClientId);
    const ClientRecord* findByUsername(const char* username) const;
    size_t localReservedCount() const;
    size_t localOnlineCount() const;
    size_t totalCount() const;
    ClientRecord* records();
    const ClientRecord* records() const;

private:
    ClientRecord* allocateClientSlot();
    UsernameClaimRecord* findClaim(const char* username, const char* stableClientId);
    const UsernameClaimRecord* findClaim(const char* username, const char* stableClientId) const;
    const UsernameClaimRecord* findClaimByUsername(const char* username) const;
    UsernameClaimRecord* allocateClaimSlot();

    char m_selfNodeId[cfg::kNodeIdLength];
    char m_selfDisplayName[cfg::kDisplayNameLength];
    uint32_t m_selfNodeHash;
    ClientRecord m_clients[cfg::kMaxKnownClients];
    UsernameClaimRecord m_claims[cfg::kMaxClaims];
};
