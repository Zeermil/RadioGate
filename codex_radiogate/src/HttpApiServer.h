#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

class NodeController;

class HttpApiServer {
public:
    explicit HttpApiServer(NodeController& controller);

    void begin();

private:
    void sendJson(AsyncWebServerRequest* request, uint16_t statusCode, JsonDocument& document);

    NodeController& m_controller;
    AsyncWebServer m_server;
};
