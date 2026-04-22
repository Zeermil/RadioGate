#pragma once

#include "mesh_types.h"

class SessionManager {
public:
    enum class AttachRejectReason : uint8_t {
        None = 0,
        UsernameMiss = 1,
        TokenMismatch = 2,
        SessionMissing = 3,
        SessionReplaced = 4,
    };

    enum class ExpireReason : uint8_t {
        Idle = 0,
        PongTimeout = 1,
    };

    struct ExpiredSession {
        char username[cfg::kUsernameLength];
        char tokenPrefix[9];
        uint32_t wsClientId;
        uint32_t attachedAtMs;
        uint32_t lastActivityMs;
        uint32_t lastPingSentMs;
        uint32_t lastPongMs;
        uint32_t idleDeltaMs;
        uint32_t pongDeltaMs;
        bool wasWsConnected;
        bool awaitingPong;
        bool futureClamped;
        ExpireReason reason;
    };

    SessionManager();

    bool createOrRefresh(const char* username,
                         const char* stableClientId,
                         const char* mac,
                         uint32_t nowMs,
                         SessionRecord& outSession);
    SessionRecord* validate(const char* username, const char* token, uint32_t nowMs);
    SessionRecord* validateByToken(const char* token, uint32_t nowMs);
    SessionRecord* findByUsername(const char* username);
    const SessionRecord* findByUsername(const char* username) const;
    SessionRecord* findByToken(const char* token);
    const SessionRecord* findByToken(const char* token) const;
    SessionRecord* findByWsClientId(uint32_t wsClientId);
    SessionRecord* attachWs(const char* username, const char* token, uint32_t wsClientId, uint32_t nowMs);
    void detachWs(uint32_t wsClientId);
    void markPingSent(uint32_t wsClientId, uint32_t nowMs);
    void markPong(uint32_t wsClientId, uint32_t nowMs);
    void touch(const char* token, uint32_t nowMs);
    bool removeByToken(const char* token, char* usernameOut, size_t usernameSize);
    size_t cleanup(uint32_t nowMs, ExpiredSession* expiredSessions, size_t capacity);
    size_t connectedCount() const;
    size_t awaitingPongCount() const;
    uint32_t latestPingSentMs() const;
    uint32_t latestPongMs() const;
    SessionRecord* records();
    const SessionRecord* records() const;

private:
    SessionRecord m_records[cfg::kMaxSessions];
};
