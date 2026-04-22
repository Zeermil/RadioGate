#include "NodeIdentity.h"

#include <WiFi.h>

#include "utils.h"

NodeIdentity::NodeIdentity() : m_info{} {}

void NodeIdentity::begin() {
    memset(&m_info, 0, sizeof(m_info));
    m_info.efuseMac = ESP.getEfuseMac();
    utils::formatNodeId(m_info.nodeId, sizeof(m_info.nodeId), m_info.efuseMac);
    utils::safeCopy(m_info.displayName, sizeof(m_info.displayName), "Lora-0");
    m_info.nodeHash = utils::fnv1a32(m_info.nodeId);
}

const NodeIdentityInfo& NodeIdentity::info() const {
    return m_info;
}

void NodeIdentity::setDisplayName(const char* displayName) {
    utils::safeCopy(m_info.displayName, sizeof(m_info.displayName), displayName);
}
