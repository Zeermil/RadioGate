#pragma once

#include "mesh_types.h"

class PendingMessageQueue {
public:
    PendingMessageQueue();

    bool add(const PendingMessage& message);
    PendingMessage* findByMessageId(const char* messageId);
    PendingMessage* findByClientMessageId(const char* clientMessageId);
    void markNodeAck(const char* messageId, uint32_t nowMs);
    void markDelivered(const char* messageId);
    void markFailed(const char* messageId);
    void removeDelivered();
    size_t cleanupExpired(uint32_t nowMs, char messageIds[][cfg::kMessageIdLength], size_t capacity);
    PendingMessage* records();
    const PendingMessage* records() const;
    size_t count() const;

private:
    PendingMessage m_records[cfg::kMaxPendingMessages];
};
