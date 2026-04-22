#include "WiFiApService.h"

#include "config.h"

bool WiFiApService::begin(const char* ssid) {
    m_ip = IPAddress(192, 168, 4, 1);
    m_gateway = IPAddress(192, 168, 4, 1);
    m_subnet = IPAddress(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(m_ip, m_gateway, m_subnet);

    return WiFi.softAP(ssid, nullptr, cfg::kApChannel, cfg::kApHidden, cfg::kApMaxClients);
}

void WiFiApService::loop() {}

const IPAddress& WiFiApService::ip() const {
    return m_ip;
}

uint8_t WiFiApService::stationCount() const {
    return WiFi.softAPgetStationNum();
}
