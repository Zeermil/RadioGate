#include "SessionManager.h"

#include <string.h>

#include "config.h"
#include "utils.h"

namespace {

void copyTokenPrefix(const char* token, char* out, size_t outSize) {
    if (outSize == 0) {
        return;
    }
    if (token == nullptr || token[0] == '\0') {
        utils::safeCopy(out, outSize, "-");
        return;
    }

    char prefix[9] = {};
    size_t index = 0;
    while (index < 8 && token[index] != '\0') {
        prefix[index] = token[index];
        ++index;
    }
    prefix[index] = '\0';
    utils::safeCopy(out, outSize, prefix);
}

}  // namespace

SessionManager::SessionManager() : m_records{} {}

bool SessionManager::createOrRefresh(const char* username,
                                     const char* stableClientId,
                                     const char* mac,
                                     uint32_t nowMs,
                                     SessionRecord& outSession) {
    SessionRecord* target = findByUsername(username);
    if (target == nullptr) {
        for (auto& record : m_records) {
            if (!record.used) {
                target = &record;
                break;
            }
        }
    }
    if (target == nullptr) {
        return false;
    }

    target->used = true;
    utils::safeCopy(target->username, sizeof(target->username), username);
    utils::safeCopy(target->stableClientId, sizeof(target->stableClientId), stableClientId);
    utils::safeCopy(target->clientMac, sizeof(target->clientMac), mac);
    target->createdAtMs = nowMs;
    target->lastActivityMs = nowMs;
    target->attachedAtMs = 0;
    target->lastPingSentMs = 0;
    target->lastPongMs = nowMs;
    target->wsClientId = 0;
    target->wsConnected = false;
    target->awaitingPong = false;
    utils::formatHexToken(target->token, sizeof(target->token), 8);
    outSession = *target;
    return true;
}

SessionRecord* SessionManager::validate(const char* username, const char* token, uint32_t nowMs) {
    for (auto& record : m_records) {
        if (record.used && strcmp(record.username, username) == 0 && strcmp(record.token, token) == 0) {
            record.lastActivityMs = nowMs;
            return &record;
        }
    }
    return nullptr;
}

SessionRecord* SessionManager::validateByToken(const char* token, uint32_t nowMs) {
    for (auto& record : m_records) {
        if (record.used && strcmp(record.token, token) == 0) {
            record.lastActivityMs = nowMs;
            return &record;
        }
    }
    return nullptr;
}

SessionRecord* SessionManager::findByUsername(const char* username) {
    for (auto& record : m_records) {
        if (record.used && strcmp(record.username, username) == 0) {
            return &record;
        }
    }
    return nullptr;
}

const SessionRecord* SessionManager::findByUsername(const char* username) const {
    for (const auto& record : m_records) {
        if (record.used && strcmp(record.username, username) == 0) {
            return &record;
        }
    }
    return nullptr;
}

SessionRecord* SessionManager::findByToken(const char* token) {
    for (auto& record : m_records) {
        if (record.used && strcmp(record.token, token) == 0) {
            return &record;
        }
    }
    return nullptr;
}

const SessionRecord* SessionManager::findByToken(const char* token) const {
    for (const auto& record : m_records) {
        if (record.used && strcmp(record.token, token) == 0) {
            return &record;
        }
    }
    return nullptr;
}

SessionRecord* SessionManager::findByWsClientId(uint32_t wsClientId) {
    for (auto& record : m_records) {
        if (record.used && record.wsClientId == wsClientId) {
            return &record;
        }
    }
    return nullptr;
}

SessionRecord* SessionManager::attachWs(const char* username, const char* token, uint32_t wsClientId, uint32_t nowMs) {
    SessionRecord* session = validate(username, token, nowMs);
    if (session == nullptr) {
        return nullptr;
    }
    session->wsClientId = wsClientId;
    session->wsConnected = true;
    session->attachedAtMs = nowMs;
    session->lastPingSentMs = 0;
    session->lastPongMs = nowMs;
    session->awaitingPong = false;
    return session;
}

void SessionManager::detachWs(uint32_t wsClientId) {
    for (auto& record : m_records) {
        if (record.used && record.wsClientId == wsClientId) {
            record.wsClientId = 0;
            record.wsConnected = false;
            record.awaitingPong = false;
        }
    }
}

void SessionManager::markPingSent(uint32_t wsClientId, uint32_t nowMs) {
    for (auto& record : m_records) {
        if (record.used && record.wsClientId == wsClientId) {
            record.lastPingSentMs = nowMs;
            record.awaitingPong = true;
        }
    }
}

void SessionManager::markPong(uint32_t wsClientId, uint32_t nowMs) {
    for (auto& record : m_records) {
        if (record.used && record.wsClientId == wsClientId) {
            record.lastPongMs = nowMs;
            record.lastActivityMs = nowMs;
            record.awaitingPong = false;
        }
    }
}

void SessionManager::touch(const char* token, uint32_t nowMs) {
    SessionRecord* session = validateByToken(token, nowMs);
    if (session != nullptr) {
        session->lastActivityMs = nowMs;
    }
}

bool SessionManager::removeByToken(const char* token, char* usernameOut, size_t usernameSize) {
    for (auto& record : m_records) {
        if (record.used && strcmp(record.token, token) == 0) {
            utils::safeCopy(usernameOut, usernameSize, record.username);
            record = SessionRecord{};
            return true;
        }
    }
    return false;
}

size_t SessionManager::cleanup(uint32_t nowMs, ExpiredSession* expiredSessions, size_t capacity) {
    size_t count = 0;
    for (auto& record : m_records) {
        if (!record.used) {
            continue;
        }

        bool idleClamped = false;
        bool pongClamped = false;
        const uint32_t idleDeltaMs = utils::elapsedMs(nowMs, record.lastActivityMs, &idleClamped);
        const uint32_t pongDeltaMs = record.lastPingSentMs > 0 ? utils::elapsedMs(nowMs, record.lastPingSentMs, &pongClamped) : 0;
        const bool idleExpired = idleDeltaMs > cfg::kSessionIdleTimeoutMs;
        const bool pongExpired =
            record.wsConnected &&
            record.awaitingPong &&
            record.lastPingSentMs > 0 &&
            pongDeltaMs > cfg::kWsPongTimeoutMs;
        if (!(idleExpired || pongExpired)) {
            continue;
        }

        if (count < capacity && expiredSessions != nullptr) {
            utils::safeCopy(expiredSessions[count].username, cfg::kUsernameLength, record.username);
            copyTokenPrefix(record.token, expiredSessions[count].tokenPrefix, sizeof(expiredSessions[count].tokenPrefix));
            expiredSessions[count].wsClientId = record.wsClientId;
            expiredSessions[count].attachedAtMs = record.attachedAtMs;
            expiredSessions[count].lastActivityMs = record.lastActivityMs;
            expiredSessions[count].lastPingSentMs = record.lastPingSentMs;
            expiredSessions[count].lastPongMs = record.lastPongMs;
            expiredSessions[count].idleDeltaMs = idleDeltaMs;
            expiredSessions[count].pongDeltaMs = pongDeltaMs;
            expiredSessions[count].wasWsConnected = record.wsConnected;
            expiredSessions[count].awaitingPong = record.awaitingPong;
            expiredSessions[count].futureClamped = idleClamped || pongClamped;
            expiredSessions[count].reason = pongExpired ? ExpireReason::PongTimeout : ExpireReason::Idle;
            ++count;
        }
        record = SessionRecord{};
    }
    return count;
}

size_t SessionManager::connectedCount() const {
    size_t count = 0;
    for (const auto& record : m_records) {
        if (record.used && record.wsConnected) {
            ++count;
        }
    }
    return count;
}

size_t SessionManager::awaitingPongCount() const {
    size_t count = 0;
    for (const auto& record : m_records) {
        if (record.used && record.wsConnected && record.awaitingPong) {
            ++count;
        }
    }
    return count;
}

uint32_t SessionManager::latestPingSentMs() const {
    uint32_t latest = 0;
    for (const auto& record : m_records) {
        if (record.used && record.lastPingSentMs > latest) {
            latest = record.lastPingSentMs;
        }
    }
    return latest;
}

uint32_t SessionManager::latestPongMs() const {
    uint32_t latest = 0;
    for (const auto& record : m_records) {
        if (record.used && record.lastPongMs > latest) {
            latest = record.lastPongMs;
        }
    }
    return latest;
}

const SessionRecord* SessionManager::records() const {
    return m_records;
}

SessionRecord* SessionManager::records() {
    return m_records;
}
