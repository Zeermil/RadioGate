#pragma once

#include "mesh_types.h"

class NeighborTable {
public:
    NeighborTable();

    void upsert(uint32_t nodeHash,
                const char* nodeId,
                const char* displayName,
                int16_t rssi,
                float snr,
                uint32_t nowMs);
    void cleanup(uint32_t nowMs);
    const NeighborRecord* find(uint32_t nodeHash) const;
    size_t aliveCount() const;
    const NeighborRecord* records() const;

private:
    NeighborRecord m_records[cfg::kMaxNeighbors];
};
