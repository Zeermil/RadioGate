#include "PendingMessageQueue.h"

#include <string.h>

#include "config.h"
#include "utils.h"

PendingMessageQueue::PendingMessageQueue() : m_records{} {}

bool PendingMessageQueue::add(const PendingMessage& message) {
    for (auto& record : m_records) {
        if (!record.used) {
            record = message;
            record.used = true;
            return true;
        }
    }
    return false;
}

PendingMessage* PendingMessageQueue::findByMessageId(const char* messageId) {
    for (auto& record : m_records) {
        if (record.used && strcmp(record.messageId, messageId) == 0) {
            return &record;
        }
    }
    return nullptr;
}

PendingMessage* PendingMessageQueue::findByClientMessageId(const char* clientMessageId) {
    for (auto& record : m_records) {
        if (record.used && strcmp(record.clientMessageId, clientMessageId) == 0) {
            return &record;
        }
    }
    return nullptr;
}

void PendingMessageQueue::markNodeAck(const char* messageId, uint32_t nowMs) {
    PendingMessage* message = findByMessageId(messageId);
    if (message != nullptr) {
        message->awaitingNodeAck = false;
        message->lastTryMs = nowMs;
        if (message->status == MessageStatus::Pending) {
            message->status = MessageStatus::Forwarded;
        }
    }
}

void PendingMessageQueue::markDelivered(const char* messageId) {
    PendingMessage* message = findByMessageId(messageId);
    if (message != nullptr) {
        message->awaitingNodeAck = false;
        message->awaitingDeliveryAck = false;
        message->awaitingLocalWsAck = false;
        message->status = MessageStatus::Delivered;
    }
}

void PendingMessageQueue::markFailed(const char* messageId) {
    PendingMessage* message = findByMessageId(messageId);
    if (message != nullptr) {
        message->awaitingNodeAck = false;
        message->awaitingDeliveryAck = false;
        message->awaitingLocalWsAck = false;
        message->status = MessageStatus::Failed;
    }
}

void PendingMessageQueue::removeDelivered() {
    for (auto& record : m_records) {
        if (record.used && record.status == MessageStatus::Delivered) {
            record = PendingMessage{};
        }
    }
}

size_t PendingMessageQueue::cleanupExpired(uint32_t nowMs, char messageIds[][cfg::kMessageIdLength], size_t capacity) {
    size_t count = 0;
    for (auto& record : m_records) {
        if (!record.used) {
            continue;
        }
        if (utils::elapsedMs(nowMs, record.createdAtMs) <= cfg::kMessageTtlMs) {
            continue;
        }
        if (count < capacity) {
            strncpy(messageIds[count], record.messageId, cfg::kMessageIdLength - 1);
            messageIds[count][cfg::kMessageIdLength - 1] = '\0';
            ++count;
        }
        record = PendingMessage{};
    }
    return count;
}

PendingMessage* PendingMessageQueue::records() {
    return m_records;
}

const PendingMessage* PendingMessageQueue::records() const {
    return m_records;
}

size_t PendingMessageQueue::count() const {
    size_t count = 0;
    for (const auto& record : m_records) {
        if (record.used) {
            ++count;
        }
    }
    return count;
}
