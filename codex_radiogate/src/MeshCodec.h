#pragma once

#include <Arduino.h>

#include "mesh_types.h"

class MeshCodec {
public:
    static constexpr size_t kHeaderSize = 26;
    static constexpr size_t kFragmentHeaderSize = 8;
    static constexpr size_t kFragmentChunkSize = cfg::kMaxLoRaPayload - kFragmentHeaderSize;

    bool encodeFrame(const MeshHeader& header,
                     const uint8_t* payload,
                     size_t payloadLength,
                     uint8_t* output,
                     size_t& outputLength) const;

    bool decodeFrame(const uint8_t* frame,
                     size_t frameLength,
                     MeshHeader& header,
                     const uint8_t*& payload,
                     size_t& payloadLength) const;

    bool encodeFragmentPayload(const FragmentHeader& fragmentHeader,
                               const uint8_t* chunk,
                               size_t chunkLength,
                               uint8_t* output,
                               size_t& outputLength) const;

    bool decodeFragmentPayload(const uint8_t* payload,
                               size_t payloadLength,
                               FragmentHeader& fragmentHeader,
                               const uint8_t*& chunk,
                               size_t& chunkLength) const;

private:
    static void writeUint16(uint8_t* buffer, uint16_t value);
    static void writeUint32(uint8_t* buffer, uint32_t value);
    static uint16_t readUint16(const uint8_t* buffer);
    static uint32_t readUint32(const uint8_t* buffer);
};
