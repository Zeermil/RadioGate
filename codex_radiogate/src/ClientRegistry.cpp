#include "ClientRegistry.h"

#include <string.h>

#include "config.h"
#include "utils.h"

ClientRegistry::ClientRegistry() : m_selfNodeHash(0), m_clients{}, m_claims{} {
    memset(m_selfNodeId, 0, sizeof(m_selfNodeId));
    memset(m_selfDisplayName, 0, sizeof(m_selfDisplayName));
}

void ClientRegistry::setSelfNode(const char* nodeId, const char* displayName, uint32_t nodeHash) {
    utils::safeCopy(m_selfNodeId, sizeof(m_selfNodeId), nodeId);
    utils::safeCopy(m_selfDisplayName, sizeof(m_selfDisplayName), displayName);
    m_selfNodeHash = nodeHash;
}

ClientRegistry::ConnectCheck ClientRegistry::beginOrResumeClaim(const char* username,
                                                                const char* stableClientId,
                                                                const char* mac,
                                                                uint32_t nowMs) {
    ConnectCheck result{ConnectDecision::Invalid, 0, false, false};
    if (!utils::isAllowedUsername(username) || stableClientId == nullptr || strlen(stableClientId) < 3 || mac == nullptr || strlen(mac) < 11) {
        return result;
    }

    ClientRecord* existingByStable = findByStableClientId(stableClientId);
    ClientRecord* existingByUsername = findByUsername(username);
    if (existingByUsername != nullptr && strcmp(existingByUsername->stableClientId, stableClientId) != 0) {
        result.decision = ConnectDecision::Conflict;
        return result;
    }
    if (existingByStable != nullptr) {
        if (strcmp(existingByStable->username, username) != 0) {
            result.decision = ConnectDecision::Conflict;
            return result;
        }
        result.decision = ConnectDecision::Ok;
        result.existingClient = true;
        return result;
    }

    if (localReservedCount() >= cfg::kMaxLocalClients) {
        result.decision = ConnectDecision::NodeFull;
        return result;
    }

    UsernameClaimRecord* claim = findClaim(username, stableClientId);
    if (claim != nullptr) {
        if (claim->conflict) {
            result.decision = ConnectDecision::Conflict;
            return result;
        }
        const uint32_t windowMs = cfg::kUsernameClaimWindowMinMs;
        const uint32_t claimAgeMs = utils::elapsedMs(nowMs, claim->createdAtMs);
        if (claimAgeMs >= windowMs) {
            result.decision = ConnectDecision::Ok;
            return result;
        }
        result.decision = ConnectDecision::Pending;
        result.retryAfterMs = windowMs - claimAgeMs;
        return result;
    }

    claim = allocateClaimSlot();
    if (claim == nullptr) {
        result.decision = ConnectDecision::NodeFull;
        return result;
    }

    claim->used = true;
    claim->conflict = false;
    utils::safeCopy(claim->username, sizeof(claim->username), username);
    utils::safeCopy(claim->stableClientId, sizeof(claim->stableClientId), stableClientId);
    utils::safeCopy(claim->mac, sizeof(claim->mac), mac);
    claim->createdAtMs = nowMs;

    result.decision = ConnectDecision::Pending;
    result.retryAfterMs = utils::jitterMs(cfg::kUsernameClaimWindowMinMs, cfg::kUsernameClaimWindowMaxMs);
    result.broadcastClaim = true;
    return result;
}

bool ClientRegistry::finalizeClaim(const char* username,
                                   const char* stableClientId,
                                   const char* mac,
                                   uint32_t nowMs,
                                   ClientRecord& outRecord) {
    UsernameClaimRecord* claim = findClaim(username, stableClientId);
    if (claim == nullptr || claim->conflict) {
        return false;
    }

    ClientRecord* record = findByStableClientId(stableClientId);
    if (record == nullptr) {
        record = findByUsername(username);
    }
    if (record == nullptr) {
        record = allocateClientSlot();
    }
    if (record == nullptr) {
        return false;
    }

    record->used = true;
    utils::safeCopy(record->username, sizeof(record->username), username);
    utils::safeCopy(record->mac, sizeof(record->mac), mac);
    utils::safeCopy(record->stableClientId, sizeof(record->stableClientId), stableClientId);
    utils::safeCopy(record->homeNodeId, sizeof(record->homeNodeId), m_selfNodeId);
    utils::safeCopy(record->displayNode, sizeof(record->displayNode), m_selfDisplayName);
    record->homeNodeHash = m_selfNodeHash;
    record->isOnline = false;
    record->isLocal = true;
    record->claimTimestampMs = claim->createdAtMs;
    record->lastSeenMs = nowMs;
    record->lastSyncSeenMs = 0;
    outRecord = *record;

    claim->used = false;
    return true;
}

void ClientRegistry::markClaimConflict(const char* username, const char* stableClientId) {
    UsernameClaimRecord* claim = findClaim(username, stableClientId);
    if (claim != nullptr) {
        claim->conflict = true;
    }
}

bool ClientRegistry::shouldRejectRemoteClaim(const char* username,
                                             const char* stableClientId,
                                             uint32_t claimTimestampMs,
                                             uint32_t remoteNodeHash) const {
    const ClientRecord* existing = findByUsername(username);
    if (existing != nullptr && strcmp(existing->stableClientId, stableClientId) != 0) {
        if (existing->claimTimestampMs < claimTimestampMs) {
            return true;
        }
        if (existing->claimTimestampMs == claimTimestampMs && existing->homeNodeHash <= remoteNodeHash) {
            return true;
        }
    }

    const UsernameClaimRecord* claim = findClaimByUsername(username);
    if (claim != nullptr && strcmp(claim->stableClientId, stableClientId) != 0) {
        if (claim->createdAtMs < claimTimestampMs) {
            return true;
        }
        if (claim->createdAtMs == claimTimestampMs && m_selfNodeHash <= remoteNodeHash) {
            return true;
        }
    }

    return false;
}

void ClientRegistry::upsertRemoteClient(const char* username,
                                        const char* stableClientId,
                                        const char* mac,
                                        uint32_t homeNodeHash,
                                        const char* homeNodeId,
                                        const char* displayNode,
                                        bool isOnline,
                                        uint32_t claimTimestampMs,
                                        uint32_t nowMs) {
    ClientRecord* existing = findByUsername(username);
    if (existing != nullptr && strcmp(existing->stableClientId, stableClientId) != 0) {
        const bool remoteWins = claimTimestampMs < existing->claimTimestampMs ||
                                (claimTimestampMs == existing->claimTimestampMs && homeNodeHash < existing->homeNodeHash);
        if (!remoteWins) {
            return;
        }
    }

    ClientRecord* record = existing;
    if (record == nullptr) {
        record = findByStableClientId(stableClientId);
    }
    if (record == nullptr) {
        record = allocateClientSlot();
    }
    if (record == nullptr) {
        return;
    }

    record->used = true;
    utils::safeCopy(record->username, sizeof(record->username), username);
    utils::safeCopy(record->mac, sizeof(record->mac), mac);
    utils::safeCopy(record->stableClientId, sizeof(record->stableClientId), stableClientId);
    utils::safeCopy(record->homeNodeId, sizeof(record->homeNodeId), homeNodeId);
    utils::safeCopy(record->displayNode, sizeof(record->displayNode), displayNode);
    record->homeNodeHash = homeNodeHash;
    record->isOnline = isOnline;
    record->isLocal = homeNodeHash == m_selfNodeHash;
    record->claimTimestampMs = claimTimestampMs;
    record->lastSeenMs = nowMs;
    record->lastSyncSeenMs = nowMs;
}

void ClientRegistry::bindSession(const char* username, uint32_t sessionHash, uint32_t nowMs) {
    ClientRecord* record = findByUsername(username);
    if (record != nullptr) {
        record->sessionHash = sessionHash;
        record->lastSeenMs = nowMs;
    }
}

void ClientRegistry::markOfflineByUsername(const char* username, uint32_t nowMs) {
    ClientRecord* record = findByUsername(username);
    if (record != nullptr) {
        record->isOnline = false;
        record->lastSeenMs = nowMs;
    }
}

void ClientRegistry::markOfflineByNode(uint32_t homeNodeHash, uint32_t nowMs) {
    for (auto& record : m_clients) {
        if (record.used && record.homeNodeHash == homeNodeHash) {
            record.isOnline = false;
            record.lastSeenMs = nowMs;
        }
    }
}

void ClientRegistry::markOnline(const char* username, uint32_t sessionHash, uint32_t nowMs) {
    ClientRecord* record = findByUsername(username);
    if (record != nullptr) {
        record->isOnline = true;
        record->sessionHash = sessionHash;
        record->lastSeenMs = nowMs;
    }
}

void ClientRegistry::clearSessionHash(const char* username) {
    ClientRecord* record = findByUsername(username);
    if (record != nullptr) {
        record->sessionHash = 0;
    }
}

void ClientRegistry::cleanupClaims(uint32_t nowMs) {
    for (auto& claim : m_claims) {
        const uint32_t claimAgeMs = utils::elapsedMs(nowMs, claim.createdAtMs);
        if (claim.used && claimAgeMs > (cfg::kUsernameClaimWindowMaxMs * 4UL)) {
            claim = UsernameClaimRecord{};
        }
    }
}

ClientRecord* ClientRegistry::upsertLocalClient(const char* username,
                                                const char* stableClientId,
                                                const char* mac,
                                                uint32_t nowMs) {
    ClientRecord* record = findByStableClientId(stableClientId);
    if (record == nullptr) {
        record = findByUsername(username);
    }
    if (record == nullptr) {
        record = allocateClientSlot();
    }
    if (record == nullptr) {
        return nullptr;
    }

    record->used = true;
    utils::safeCopy(record->username, sizeof(record->username), username);
    utils::safeCopy(record->mac, sizeof(record->mac), mac);
    utils::safeCopy(record->stableClientId, sizeof(record->stableClientId), stableClientId);
    utils::safeCopy(record->homeNodeId, sizeof(record->homeNodeId), m_selfNodeId);
    utils::safeCopy(record->displayNode, sizeof(record->displayNode), m_selfDisplayName);
    record->homeNodeHash = m_selfNodeHash;
    record->isOnline = false;
    record->isLocal = true;
    record->claimTimestampMs = nowMs;
    record->lastSeenMs = nowMs;
    record->lastSyncSeenMs = 0;
    return record;
}

void ClientRegistry::removeRemoteByUsername(const char* username) {
    ClientRecord* record = findByUsername(username);
    if (record != nullptr && !record->isLocal) {
        *record = ClientRecord{};
    }
}

void ClientRegistry::removeRemoteByNode(uint32_t homeNodeHash) {
    for (auto& record : m_clients) {
        if (record.used && !record.isLocal && record.homeNodeHash == homeNodeHash) {
            record = ClientRecord{};
        }
    }
}

ClientRecord* ClientRegistry::findByUsername(const char* username) {
    for (auto& record : m_clients) {
        if (record.used && strcmp(record.username, username) == 0) {
            return &record;
        }
    }
    return nullptr;
}

const ClientRecord* ClientRegistry::findByUsername(const char* username) const {
    for (const auto& record : m_clients) {
        if (record.used && strcmp(record.username, username) == 0) {
            return &record;
        }
    }
    return nullptr;
}

ClientRecord* ClientRegistry::findByStableClientId(const char* stableClientId) {
    for (auto& record : m_clients) {
        if (record.used && strcmp(record.stableClientId, stableClientId) == 0) {
            return &record;
        }
    }
    return nullptr;
}

size_t ClientRegistry::localReservedCount() const {
    size_t count = 0;
    for (const auto& record : m_clients) {
        if (record.used && record.isLocal && record.sessionHash != 0) {
            ++count;
        }
    }
    return count;
}

size_t ClientRegistry::localOnlineCount() const {
    size_t count = 0;
    for (const auto& record : m_clients) {
        if (record.used && record.isLocal && record.isOnline) {
            ++count;
        }
    }
    return count;
}

size_t ClientRegistry::totalCount() const {
    size_t count = 0;
    for (const auto& record : m_clients) {
        if (record.used) {
            ++count;
        }
    }
    return count;
}

ClientRecord* ClientRegistry::records() {
    return m_clients;
}

const ClientRecord* ClientRegistry::records() const {
    return m_clients;
}

ClientRecord* ClientRegistry::allocateClientSlot() {
    for (auto& record : m_clients) {
        if (!record.used) {
            return &record;
        }
    }
    return nullptr;
}

UsernameClaimRecord* ClientRegistry::findClaim(const char* username, const char* stableClientId) {
    for (auto& claim : m_claims) {
        if (claim.used && strcmp(claim.username, username) == 0 && strcmp(claim.stableClientId, stableClientId) == 0) {
            return &claim;
        }
    }
    return nullptr;
}

const UsernameClaimRecord* ClientRegistry::findClaim(const char* username, const char* stableClientId) const {
    for (const auto& claim : m_claims) {
        if (claim.used && strcmp(claim.username, username) == 0 && strcmp(claim.stableClientId, stableClientId) == 0) {
            return &claim;
        }
    }
    return nullptr;
}

const UsernameClaimRecord* ClientRegistry::findClaimByUsername(const char* username) const {
    for (const auto& claim : m_claims) {
        if (claim.used && strcmp(claim.username, username) == 0) {
            return &claim;
        }
    }
    return nullptr;
}

UsernameClaimRecord* ClientRegistry::allocateClaimSlot() {
    for (auto& claim : m_claims) {
        if (!claim.used) {
            return &claim;
        }
    }
    return nullptr;
}
