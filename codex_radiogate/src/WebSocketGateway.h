#pragma once

#include <ESPAsyncWebServer.h>

class NodeController;

class WebSocketGateway {
public:
    enum class SendResult : uint8_t {
        Sent = 0,
        Busy = 1,
        Missing = 2,
    };

    explicit WebSocketGateway(NodeController& controller);

    void begin();
    void loop();
    SendResult sendToClient(uint32_t clientId, const char* payload);
    SendResult pingClient(uint32_t clientId);
    bool closeClient(uint32_t clientId, uint16_t code, const char* message);

private:
    void onEvent(AsyncWebSocket* server,
                 AsyncWebSocketClient* client,
                 AwsEventType type,
                 void* arg,
                 uint8_t* data,
                 size_t len);

    NodeController& m_controller;
    AsyncWebServer m_server;
    AsyncWebSocket m_socket;
};
