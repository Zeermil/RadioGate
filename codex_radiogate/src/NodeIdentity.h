#pragma once

#include "mesh_types.h"

class NodeIdentity {
public:
    NodeIdentity();

    void begin();
    const NodeIdentityInfo& info() const;
    void setDisplayName(const char* displayName);

private:
    NodeIdentityInfo m_info;
};
