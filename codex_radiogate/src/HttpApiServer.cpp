#include "HttpApiServer.h"

#include <AsyncJson.h>
#include <ArduinoJson.h>

#include "NodeController.h"

HttpApiServer::HttpApiServer(NodeController& controller) : m_controller(controller), m_server(cfg::kHttpPort) {}

void HttpApiServer::begin() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, X-Username, X-Session-Token");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");

    m_server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument document;
        const uint32_t nowMs = m_controller.captureNowMs();
        m_controller.fillStatus(document, nowMs);
        sendJson(request, 200, document);
    });

    m_server.on("/api/clients", HTTP_GET, [this](AsyncWebServerRequest* request) {
        const String username = request->header("X-Username");
        const String token = request->header("X-Session-Token");
        JsonDocument document;
        const uint32_t nowMs = m_controller.captureNowMs();
        const uint16_t status = m_controller.buildClientsResponse(username.c_str(), token.c_str(), document, nowMs);
        sendJson(request, status, document);
    });

    auto* connectHandler = new AsyncCallbackJsonWebHandler("/api/connect", [this](AsyncWebServerRequest* request, JsonVariant& json) {
        JsonObject body = json.as<JsonObject>();
        const String username = body["username"] | "";
        const String clientId = body["clientId"] | "";
        const String mac = body["mac"] | "";
        const uint32_t nowMs = m_controller.captureNowMs();
        const NodeController::ConnectReply reply = m_controller.handleConnect(username.c_str(), clientId.c_str(), mac.c_str(), nowMs);

        JsonDocument document;
        document["status"] = reply.status;
        if (reply.httpStatus == 200) {
            document["username"] = reply.username;
            document["nodeId"] = m_controller.identity().nodeId;
            document["displayName"] = m_controller.identity().displayName;
            document["sessionToken"] = reply.sessionToken;
            document["sessionTtlSec"] = cfg::kSessionIdleTimeoutMs / 1000UL;
        } else {
            document["code"] = reply.code;
            if (reply.retryAfterMs > 0) {
                document["retryAfterMs"] = reply.retryAfterMs;
            }
            if (strlen(reply.message) > 0) {
                document["message"] = reply.message;
            }
        }
        sendJson(request, reply.httpStatus, document);
    });
    m_server.addHandler(connectHandler);

    auto* sendHandler = new AsyncCallbackJsonWebHandler("/api/send", [this](AsyncWebServerRequest* request, JsonVariant& json) {
        JsonObject body = json.as<JsonObject>();
        const String token = body["sessionToken"] | "";
        const String to = body["to"] | "";
        const String clientMessageId = body["clientMessageId"] | "";
        const String content = body["content"] | "";
        const uint32_t nowMs = m_controller.captureNowMs();
        const NodeController::SendReply reply = m_controller.handleSend(token.c_str(), to.c_str(), clientMessageId.c_str(), content.c_str(), nowMs);

        JsonDocument document;
        document["status"] = reply.status;
        if (reply.httpStatus == 202) {
            document["messageId"] = reply.messageId;
            document["deliveryStatus"] = "pending";
        } else {
            document["code"] = reply.code;
            document["message"] = reply.message;
        }
        sendJson(request, reply.httpStatus, document);
    });
    m_server.addHandler(sendHandler);

    auto* disconnectHandler = new AsyncCallbackJsonWebHandler("/api/disconnect", [this](AsyncWebServerRequest* request, JsonVariant& json) {
        JsonObject body = json.as<JsonObject>();
        const String token = body["sessionToken"] | "";
        const uint32_t nowMs = m_controller.captureNowMs();
        const NodeController::DisconnectReply reply = m_controller.handleDisconnect(token.c_str(), nowMs);

        JsonDocument document;
        document["status"] = reply.status;
        if (reply.httpStatus != 200) {
            document["code"] = reply.code;
            document["message"] = reply.message;
        }
        sendJson(request, reply.httpStatus, document);
    });
    m_server.addHandler(disconnectHandler);

    m_server.on("^/api/.*", HTTP_OPTIONS, [this](AsyncWebServerRequest* request) {
        JsonDocument document;
        document["status"] = "ok";
        sendJson(request, 200, document);
    });

    m_server.begin();
}

void HttpApiServer::sendJson(AsyncWebServerRequest* request, uint16_t statusCode, JsonDocument& document) {
    String payload;
    serializeJson(document, payload);
    AsyncWebServerResponse* response = request->beginResponse(statusCode, "application/json", payload);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}
