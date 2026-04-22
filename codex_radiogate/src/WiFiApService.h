#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WiFiApService {
public:
    bool begin(const char* ssid);
    void loop();

    const IPAddress& ip() const;
    uint8_t stationCount() const;

private:
    IPAddress m_ip;
    IPAddress m_gateway;
    IPAddress m_subnet;
};
