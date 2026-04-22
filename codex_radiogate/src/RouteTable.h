#pragma once

#include "mesh_types.h"

class RouteTable {
public:
    RouteTable();

    void learnDirect(uint32_t nodeHash, const char* nodeId, int16_t rssi, uint32_t nowMs);
    void learnRoute(uint32_t dstNodeHash,
                    const char* dstNodeId,
                    uint32_t nextHopHash,
                    const char* nextHopNodeId,
                    uint8_t hopCount,
                    int16_t rssi,
                    uint32_t nowMs);
    const RouteRecord* find(uint32_t dstNodeHash) const;
    void invalidateNextHop(uint32_t nextHopHash);
    void cleanup(uint32_t nowMs);
    const RouteRecord* records() const;
    size_t count() const;

private:
    RouteRecord m_records[cfg::kMaxRoutes];
};
