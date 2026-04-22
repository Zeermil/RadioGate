#include "MeshProtocol.h"

#include <string.h>

#include "utils.h"

namespace meshproto {

namespace {

bool writeByte(uint8_t* output, size_t outputSize, size_t& offset, uint8_t value) {
    if (offset + 1 > outputSize) {
        return false;
    }
    output[offset++] = value;
    return true;
}

bool writeUInt16(uint8_t* output, size_t outputSize, size_t& offset, uint16_t value) {
    return writeByte(output, outputSize, offset, static_cast<uint8_t>(value & 0xFFU)) &&
           writeByte(output, outputSize, offset, static_cast<uint8_t>((value >> 8) & 0xFFU));
}

bool writeUInt32(uint8_t* output, size_t outputSize, size_t& offset, uint32_t value) {
    return writeByte(output, outputSize, offset, static_cast<uint8_t>(value & 0xFFU)) &&
           writeByte(output, outputSize, offset, static_cast<uint8_t>((value >> 8) & 0xFFU)) &&
           writeByte(output, outputSize, offset, static_cast<uint8_t>((value >> 16) & 0xFFU)) &&
           writeByte(output, outputSize, offset, static_cast<uint8_t>((value >> 24) & 0xFFU));
}

bool writeFixedBytes(uint8_t* output, size_t outputSize, size_t& offset, const void* data, size_t size) {
    if (offset + size > outputSize) {
        return false;
    }
    memcpy(output + offset, data, size);
    offset += size;
    return true;
}

bool writeSizedString(uint8_t* output, size_t outputSize, size_t& offset, const char* value, size_t maxSize) {
    const size_t actualLength = min(strlen(value), maxSize - 1);
    if (actualLength > 255U) {
        return false;
    }
    return writeByte(output, outputSize, offset, static_cast<uint8_t>(actualLength)) &&
           writeFixedBytes(output, outputSize, offset, value, actualLength);
}

bool readByte(const uint8_t* payload, size_t payloadSize, size_t& offset, uint8_t& value) {
    if (offset + 1 > payloadSize) {
        return false;
    }
    value = payload[offset++];
    return true;
}

bool readUInt16(const uint8_t* payload, size_t payloadSize, size_t& offset, uint16_t& value) {
    uint8_t low = 0;
    uint8_t high = 0;
    if (!readByte(payload, payloadSize, offset, low) || !readByte(payload, payloadSize, offset, high)) {
        return false;
    }
    value = static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8));
    return true;
}

bool readUInt32(const uint8_t* payload, size_t payloadSize, size_t& offset, uint32_t& value) {
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    uint8_t b3 = 0;
    if (!readByte(payload, payloadSize, offset, b0) ||
        !readByte(payload, payloadSize, offset, b1) ||
        !readByte(payload, payloadSize, offset, b2) ||
        !readByte(payload, payloadSize, offset, b3)) {
        return false;
    }
    value = static_cast<uint32_t>(b0) |
            (static_cast<uint32_t>(b1) << 8) |
            (static_cast<uint32_t>(b2) << 16) |
            (static_cast<uint32_t>(b3) << 24);
    return true;
}

bool readFixedBytes(const uint8_t* payload, size_t payloadSize, size_t& offset, void* target, size_t size) {
    if (offset + size > payloadSize) {
        return false;
    }
    memcpy(target, payload + offset, size);
    offset += size;
    return true;
}

bool readSizedString(const uint8_t* payload, size_t payloadSize, size_t& offset, char* destination, size_t destinationSize) {
    uint8_t length = 0;
    if (!readByte(payload, payloadSize, offset, length)) {
        return false;
    }
    if (offset + length > payloadSize) {
        return false;
    }
    const size_t copyLength = min(static_cast<size_t>(length), destinationSize - 1);
    memcpy(destination, payload + offset, copyLength);
    destination[copyLength] = '\0';
    offset += length;
    return true;
}

bool writeEnvelopeHeader(uint8_t* output, size_t outputSize, size_t& offset, Kind kind) {
    return writeByte(output, outputSize, offset, static_cast<uint8_t>(kind)) &&
           writeByte(output, outputSize, offset, kProtocolVersion);
}

bool readAndValidateHeader(const uint8_t* payload, size_t payloadSize, size_t& offset, Kind expectedKind) {
    uint8_t kind = 0;
    uint8_t version = 0;
    return readByte(payload, payloadSize, offset, kind) &&
           readByte(payload, payloadSize, offset, version) &&
           kind == static_cast<uint8_t>(expectedKind) &&
           version == kProtocolVersion;
}

}  // namespace

bool encodePresenceSnapshot(const PresenceSnapshot& snapshot, uint8_t* output, size_t outputSize, size_t& writtenSize) {
    size_t offset = 0;
    if (!writeEnvelopeHeader(output, outputSize, offset, Kind::PresenceSnapshot) ||
        !writeUInt16(output, outputSize, offset, snapshot.meshAddress) ||
        !writeUInt32(output, outputSize, offset, snapshot.nodeHash) ||
        !writeFixedBytes(output, outputSize, offset, snapshot.nodeId, sizeof(snapshot.nodeId)) ||
        !writeFixedBytes(output, outputSize, offset, snapshot.displayName, sizeof(snapshot.displayName)) ||
        !writeByte(output, outputSize, offset, snapshot.userCount)) {
        return false;
    }

    for (size_t index = 0; index < snapshot.userCount && index < cfg::kMaxLocalClients; ++index) {
        if (!writeSizedString(output, outputSize, offset, snapshot.usernames[index], cfg::kUsernameLength)) {
            return false;
        }
    }

    writtenSize = offset;
    return true;
}

bool decodePresenceSnapshot(const uint8_t* payload, size_t payloadSize, PresenceSnapshot& snapshot) {
    size_t offset = 0;
    memset(&snapshot, 0, sizeof(snapshot));
    if (!readAndValidateHeader(payload, payloadSize, offset, Kind::PresenceSnapshot) ||
        !readUInt16(payload, payloadSize, offset, snapshot.meshAddress) ||
        !readUInt32(payload, payloadSize, offset, snapshot.nodeHash) ||
        !readFixedBytes(payload, payloadSize, offset, snapshot.nodeId, sizeof(snapshot.nodeId)) ||
        !readFixedBytes(payload, payloadSize, offset, snapshot.displayName, sizeof(snapshot.displayName)) ||
        !readByte(payload, payloadSize, offset, snapshot.userCount)) {
        return false;
    }

    snapshot.nodeId[sizeof(snapshot.nodeId) - 1] = '\0';
    snapshot.displayName[sizeof(snapshot.displayName) - 1] = '\0';
    if (snapshot.userCount > cfg::kMaxLocalClients) {
        return false;
    }

    for (size_t index = 0; index < snapshot.userCount; ++index) {
        if (!readSizedString(payload, payloadSize, offset, snapshot.usernames[index], sizeof(snapshot.usernames[index]))) {
            return false;
        }
    }
    return true;
}

bool encodeChatEnvelope(const ChatEnvelope& envelope, uint8_t* output, size_t outputSize, size_t& writtenSize) {
    size_t offset = 0;
    const size_t contentLength = min(strlen(envelope.content), static_cast<size_t>(cfg::kMaxMessageText));
    if (!writeEnvelopeHeader(output, outputSize, offset, Kind::ChatEnvelope) ||
        !writeUInt16(output, outputSize, offset, envelope.senderMeshAddress) ||
        !writeUInt32(output, outputSize, offset, envelope.senderNodeHash) ||
        !writeUInt32(output, outputSize, offset, envelope.recipientNodeHash) ||
        !writeUInt32(output, outputSize, offset, envelope.timestampMs) ||
        !writeSizedString(output, outputSize, offset, envelope.fromUsername, cfg::kUsernameLength) ||
        !writeSizedString(output, outputSize, offset, envelope.toUsername, cfg::kUsernameLength) ||
        !writeSizedString(output, outputSize, offset, envelope.messageId, cfg::kMessageIdLength) ||
        !writeUInt16(output, outputSize, offset, static_cast<uint16_t>(contentLength)) ||
        !writeFixedBytes(output, outputSize, offset, envelope.content, contentLength)) {
        return false;
    }

    writtenSize = offset;
    return true;
}

bool decodeChatEnvelope(const uint8_t* payload, size_t payloadSize, ChatEnvelope& envelope) {
    size_t offset = 0;
    uint16_t contentLength = 0;
    memset(&envelope, 0, sizeof(envelope));
    if (!readAndValidateHeader(payload, payloadSize, offset, Kind::ChatEnvelope) ||
        !readUInt16(payload, payloadSize, offset, envelope.senderMeshAddress) ||
        !readUInt32(payload, payloadSize, offset, envelope.senderNodeHash) ||
        !readUInt32(payload, payloadSize, offset, envelope.recipientNodeHash) ||
        !readUInt32(payload, payloadSize, offset, envelope.timestampMs) ||
        !readSizedString(payload, payloadSize, offset, envelope.fromUsername, sizeof(envelope.fromUsername)) ||
        !readSizedString(payload, payloadSize, offset, envelope.toUsername, sizeof(envelope.toUsername)) ||
        !readSizedString(payload, payloadSize, offset, envelope.messageId, sizeof(envelope.messageId)) ||
        !readUInt16(payload, payloadSize, offset, contentLength)) {
        return false;
    }
    if (contentLength > cfg::kMaxMessageText || offset + contentLength > payloadSize) {
        return false;
    }
    memcpy(envelope.content, payload + offset, contentLength);
    envelope.content[contentLength] = '\0';
    return true;
}

bool encodeDeliveryAck(const DeliveryAck& ack, uint8_t* output, size_t outputSize, size_t& writtenSize) {
    size_t offset = 0;
    if (!writeEnvelopeHeader(output, outputSize, offset, Kind::DeliveryAck) ||
        !writeUInt16(output, outputSize, offset, ack.senderMeshAddress) ||
        !writeUInt32(output, outputSize, offset, ack.senderNodeHash) ||
        !writeSizedString(output, outputSize, offset, ack.messageId, cfg::kMessageIdLength)) {
        return false;
    }
    writtenSize = offset;
    return true;
}

bool decodeDeliveryAck(const uint8_t* payload, size_t payloadSize, DeliveryAck& ack) {
    size_t offset = 0;
    memset(&ack, 0, sizeof(ack));
    return readAndValidateHeader(payload, payloadSize, offset, Kind::DeliveryAck) &&
           readUInt16(payload, payloadSize, offset, ack.senderMeshAddress) &&
           readUInt32(payload, payloadSize, offset, ack.senderNodeHash) &&
           readSizedString(payload, payloadSize, offset, ack.messageId, sizeof(ack.messageId));
}

bool encodeDeliveryFail(const DeliveryFail& fail, uint8_t* output, size_t outputSize, size_t& writtenSize) {
    size_t offset = 0;
    if (!writeEnvelopeHeader(output, outputSize, offset, Kind::DeliveryFail) ||
        !writeUInt16(output, outputSize, offset, fail.senderMeshAddress) ||
        !writeUInt32(output, outputSize, offset, fail.senderNodeHash) ||
        !writeByte(output, outputSize, offset, fail.failure) ||
        !writeSizedString(output, outputSize, offset, fail.messageId, cfg::kMessageIdLength)) {
        return false;
    }
    writtenSize = offset;
    return true;
}

bool decodeDeliveryFail(const uint8_t* payload, size_t payloadSize, DeliveryFail& fail) {
    size_t offset = 0;
    memset(&fail, 0, sizeof(fail));
    return readAndValidateHeader(payload, payloadSize, offset, Kind::DeliveryFail) &&
           readUInt16(payload, payloadSize, offset, fail.senderMeshAddress) &&
           readUInt32(payload, payloadSize, offset, fail.senderNodeHash) &&
           readByte(payload, payloadSize, offset, fail.failure) &&
           readSizedString(payload, payloadSize, offset, fail.messageId, sizeof(fail.messageId));
}

Kind peekKind(const uint8_t* payload, size_t payloadSize, bool& valid) {
    valid = payload != nullptr && payloadSize >= 2 && payload[1] == kProtocolVersion;
    return valid ? static_cast<Kind>(payload[0]) : Kind::PresenceSnapshot;
}

}  // namespace meshproto
