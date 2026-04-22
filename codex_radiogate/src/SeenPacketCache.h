#pragma once

#include "mesh_types.h"

class SeenPacketCache {
public:
    SeenPacketCache();

    bool contains(uint32_t originNodeHash, uint32_t packetId, uint32_t nowMs);
    void add(uint32_t originNodeHash, uint32_t packetId, uint32_t nowMs);
    void cleanup(uint32_t nowMs);
    size_t size(uint32_t nowMs) const;

private:
    SeenPacketEntry m_entries[cfg::kMaxSeenPackets];
};
