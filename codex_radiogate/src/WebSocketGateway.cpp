#include "WebSocketGateway.h"

#include "NodeController.h"

WebSocketGateway::WebSocketGateway(NodeController& controller)
    : m_controller(controller), m_server(cfg::kWsPort), m_socket("/ws") {}

void WebSocketGateway::begin() {
    m_socket.onEvent([this](AsyncWebSocket* server,
                            AsyncWebSocketClient* client,
                            AwsEventType type,
                            void* arg,
                            uint8_t* data,
                            size_t len) {
        onEvent(server, client, type, arg, data, len);
    });
    m_server.addHandler(&m_socket);
    m_server.begin();
}

void WebSocketGateway::loop() {
    m_socket.cleanupClients();
}

WebSocketGateway::SendResult WebSocketGateway::sendToClient(uint32_t clientId, const char* payload) {
    AsyncWebSocketClient* client = m_socket.client(clientId);
    if (client == nullptr) {
        return SendResult::Missing;
    }
    if (client->queueIsFull() || !client->canSend()) {
        return SendResult::Busy;
    }
    client->text(payload);
    return SendResult::Sent;
}

WebSocketGateway::SendResult WebSocketGateway::pingClient(uint32_t clientId) {
    AsyncWebSocketClient* client = m_socket.client(clientId);
    if (client == nullptr) {
        return SendResult::Missing;
    }
    if (client->queueIsFull() || !client->canSend()) {
        return SendResult::Busy;
    }
    client->ping();
    return SendResult::Sent;
}

bool WebSocketGateway::closeClient(uint32_t clientId, uint16_t code, const char* message) {
    AsyncWebSocketClient* client = m_socket.client(clientId);
    if (client == nullptr) {
        return false;
    }
    m_socket.close(clientId, code, message);
    return true;
}

void WebSocketGateway::onEvent(AsyncWebSocket*,
                               AsyncWebSocketClient* client,
                               AwsEventType type,
                               void* arg,
                               uint8_t* data,
                               size_t len) {
    if (type == WS_EVT_CONNECT) {
        AsyncWebServerRequest* request = reinterpret_cast<AsyncWebServerRequest*>(arg);
        const String username = request->getParam("username") != nullptr ? request->getParam("username")->value() : "";
        const String token = request->getParam("token") != nullptr ? request->getParam("token")->value() : "";
        const uint32_t nowMs = m_controller.captureNowMs();
        if (!m_controller.authorizeWebSocket(username.c_str(), token.c_str(), client->id(), nowMs)) {
            client->close(4001, "unauthorized");
        }
        return;
    }

    if (type == WS_EVT_DISCONNECT) {
        m_controller.handleWebSocketDisconnect(client->id(), m_controller.captureNowMs());
        return;
    }

    if (type == WS_EVT_PONG) {
        m_controller.handleWebSocketPong(client->id(), m_controller.captureNowMs());
        return;
    }

    if (type != WS_EVT_DATA) {
        return;
    }

    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
    if (info == nullptr || !info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) {
        return;
    }

    char payload[cfg::kWsTextLength];
    const size_t copyLength = min(len, sizeof(payload) - 1);
    memcpy(payload, data, copyLength);
    payload[copyLength] = '\0';
    m_controller.handleWebSocketText(client->id(), payload, copyLength, m_controller.captureNowMs());
}
