#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LoRa.h>
#include <ArduinoJson.h>

class RadioGateNode {
 public:
  void begin() {
    Serial.begin(115200);
    randomSeed(esp_random());

    nodeHash_ = makeNodeHash();
    buildNodeId(nodeHash_, nodeId_, sizeof(nodeId_));

    memset(seenPackets_, 0, sizeof(seenPackets_));
    memset(clients_, 0, sizeof(clients_));
    memset(sessions_, 0, sizeof(sessions_));
    memset(neighbors_, 0, sizeof(neighbors_));
    memset(routes_, 0, sizeof(routes_));
    memset(pending_, 0, sizeof(pending_));
    memset(txQueue_, 0, sizeof(txQueue_));
    memset(wsBindings_, 0, sizeof(wsBindings_));

    delay(random(START_RANDOM_MIN_MS, START_RANDOM_MAX_MS + 1));

    chooseDisplayName();
    setupWiFiAp();
    setupHttp();
    setupWebSocket();
    setupLoRa();

    startedAtMs_ = millis();
    lastHeartbeatMs_ = 0;
    sendNodeHello();

    Serial.printf("[BOOT] nodeId=%s displayName=%s nodeHash=%08lX freq=%lu\n", nodeId_, displayName_, static_cast<unsigned long>(nodeHash_), static_cast<unsigned long>(LORA_FREQUENCY));
  }

  void loop() {
    web_.handleClient();
    ws_.loop();

    handleLoRaRx();
    processLoRaTxQueue();
    processPendingMessages();
    cleanupSeenPackets();
    cleanupSessions();
    cleanupPendingMessages();
    cleanupNeighbors();
    sendHeartbeatIfNeeded();
    pingWsIfNeeded();
  }

 private:
  static constexpr uint16_t MESH_MAGIC = 0xCAFE;
  static constexpr uint8_t MESH_VERSION = 1;
  static constexpr uint8_t FLAG_BROADCAST = 0x01;
  static constexpr uint8_t FLAG_ACK_REQUIRED = 0x02;

  static constexpr uint32_t BROADCAST_NODE_HASH = 0xFFFFFFFFUL;

  static constexpr uint32_t START_RANDOM_MIN_MS = 500;
  static constexpr uint32_t START_RANDOM_MAX_MS = 3000;
  static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;
  static constexpr uint32_t NODE_TIMEOUT_MS = 90000;

  static constexpr uint32_t SESSION_IDLE_TIMEOUT_MS = 30UL * 60UL * 1000UL;
  static constexpr uint32_t CLIENT_OFFLINE_GRACE_MS = 45000;
  static constexpr uint32_t WS_PING_INTERVAL_MS = 15000;
  static constexpr uint32_t WS_PONG_TIMEOUT_MS = 5000;

  static constexpr uint32_t SEEN_PACKET_TTL_MS = 180000;

  static constexpr uint8_t DEFAULT_TTL = 4;
  static constexpr uint8_t MSG_MAX_RETRY = 3;
  static constexpr uint32_t MSG_RETRY_DELAY_MS = 10000;
  static constexpr uint32_t MSG_TTL_MS = 300000;

  static constexpr int AP_CHANNEL = 6;
  static constexpr int AP_MAX_CLIENTS = 8;

  static constexpr uint8_t MAX_KNOWN_CLIENTS = 32;
  static constexpr uint8_t MAX_LOCAL_CLIENTS = 4;
  static constexpr uint8_t MAX_SESSIONS = 8;
  static constexpr uint8_t MAX_NEIGHBORS = 16;
  static constexpr uint8_t MAX_ROUTES = 32;
  static constexpr uint8_t MAX_SEEN_PACKETS = 80;
  static constexpr uint8_t MAX_PENDING_MESSAGES = 30;
  static constexpr uint8_t MAX_TX_QUEUE = 48;
  static constexpr uint8_t MAX_WS_BINDINGS = 8;

  static constexpr uint16_t MAX_MESH_PAYLOAD = 180;
  static constexpr uint16_t MAX_MESSAGE_TEXT = 160;
  static constexpr uint16_t MAX_WS_MESSAGE_LENGTH = 512;
  static constexpr uint16_t MESH_HEADER_SIZE = 26;
  static constexpr uint16_t MAX_LORA_PACKET_SIZE = 230;
  static constexpr uint8_t INVALID_WS_CLIENT_NUM = 255;
  static constexpr uint8_t INVALID_PRIORITY = 255;
  static constexpr uint8_t MAX_DISPLAY_NAME_SUFFIX = 100;
  static constexpr uint8_t SESSION_TOKEN_LENGTH = 16;
  static constexpr uint8_t MIN_USERNAME_LENGTH = 3;
  static constexpr uint8_t MAX_USERNAME_LENGTH = 24;
  static constexpr uint8_t MIN_MAC_LENGTH = 8;
  static constexpr uint8_t MAX_MAC_LENGTH = 18;

  static constexpr uint8_t LORA_PIN_CS = 18;
  static constexpr uint8_t LORA_PIN_RST = 14;
  static constexpr uint8_t LORA_PIN_IRQ = 26;
  static constexpr uint32_t FALLBACK_NODE_HASH = 0xA5A5A5A5;
  static constexpr uint8_t PACKET_ID_SALT_MESH = 0x5A;
  static constexpr uint8_t PACKET_ID_SALT_ACK = 0x7D;

  static constexpr uint32_t LORA_FREQUENCY = 433000000UL;
  static constexpr long LORA_BW = 125E3;
  static constexpr uint8_t LORA_SF = 9;
  static constexpr uint8_t LORA_CR = 5;
  static constexpr int8_t LORA_TX_POWER = 17;
  static constexpr uint8_t LORA_SYNC_WORD = 0x12;

  static constexpr const char* NODE_NAME_PREFIX = "Lora-";

  enum PacketType : uint8_t {
    NODE_HELLO = 0x01,
    NODE_HEARTBEAT = 0x02,
    NODE_SYNC_REQUEST = 0x04,
    NODE_SYNC_RESPONSE = 0x05,
    CLIENT_UPDATE = 0x12,
    MSG_FORWARD = 0x20,
    MSG_NODE_ACK = 0x21,
    MSG_DELIVERY_ACK = 0x22,
    MSG_DELIVERY_FAIL = 0x23
  };

  enum MessageStatus : uint8_t {
    MSG_PENDING = 0,
    MSG_FORWARDED = 1,
    MSG_DELIVERED = 2,
    MSG_FAILED = 3
  };

  struct MeshHeader {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t ttl;
    uint8_t flags;
    uint32_t originNodeHash;
    uint32_t srcNodeHash;
    uint32_t dstNodeHash;
    uint32_t packetSeq;
    uint32_t packetId;
  } __attribute__((packed));

  struct SeenPacket {
    uint32_t packetId;
    uint32_t originNodeHash;
    uint32_t seenAtMs;
    bool used;
  };

  struct ClientRecord {
    char username[24];
    char mac[18];
    uint32_t homeNodeHash;
    bool isOnline;
    bool isLocal;
    uint32_t lastSeenMs;
    uint32_t sessionHash;
    uint32_t updatedAtMs;
    bool used;
  };

  struct SessionRecord {
    char username[24];
    char token[17];
    char clientMac[18];
    uint32_t createdAtMs;
    uint32_t lastActivityMs;
    uint32_t lastPongMs;
    bool wsConnected;
    bool used;
  };

  struct NeighborRecord {
    uint32_t nodeHash;
    uint32_t lastSeenMs;
    int16_t lastRssi;
    float lastSnr;
    uint8_t missedHeartbeats;
    bool used;
  };

  struct RouteRecord {
    uint32_t dstNodeHash;
    uint32_t nextHopHash;
    uint8_t hopCount;
    int16_t rssi;
    uint32_t updatedAtMs;
    bool used;
  };

  struct PendingMessage {
    char messageId[24];
    char fromUsername[24];
    char toUsername[24];
    char content[MAX_MESSAGE_TEXT + 1];
    uint32_t dstNodeHash;
    uint32_t originNodeHash;
    uint32_t createdAtMs;
    uint32_t lastTryMs;
    uint8_t retryCount;
    MessageStatus status;
    bool waitingForClientAck;
    bool used;
  };

  struct TxQueueItem {
    MeshHeader header;
    uint8_t payload[MAX_MESH_PAYLOAD];
    uint16_t payloadLen;
    uint32_t dueAtMs;
    uint8_t priority;
    bool used;
  };

  struct WsBinding {
    uint8_t clientNum;
    char username[24];
    bool used;
  };

  WebServer web_{80};
  WebSocketsServer ws_{81};

  char nodeId_[16] = {0};
  char displayName_[16] = {0};
  uint32_t nodeHash_ = 0;
  uint32_t packetSeq_ = 0;

  uint32_t startedAtMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t lastWsPingMs_ = 0;
  uint32_t minFreeHeap_ = UINT32_MAX;

  SeenPacket seenPackets_[MAX_SEEN_PACKETS];
  ClientRecord clients_[MAX_KNOWN_CLIENTS];
  SessionRecord sessions_[MAX_SESSIONS];
  NeighborRecord neighbors_[MAX_NEIGHBORS];
  RouteRecord routes_[MAX_ROUTES];
  PendingMessage pending_[MAX_PENDING_MESSAGES];
  TxQueueItem txQueue_[MAX_TX_QUEUE];
  WsBinding wsBindings_[MAX_WS_BINDINGS];

  static uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t seed = 2166136261UL) {
    uint32_t h = seed;
    for (size_t i = 0; i < len; ++i) {
      h ^= data[i];
      h *= 16777619UL;
    }
    return h;
  }

  static uint32_t hashText(const char* s) {
    return fnv1a(reinterpret_cast<const uint8_t*>(s), strlen(s));
  }

  static bool copySafe(char* dst, size_t dstSize, const char* src) {
    if (!dst || !src || dstSize == 0) return false;
    size_t n = strnlen(src, dstSize);
    if (n >= dstSize) return false;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return true;
  }

  uint32_t makeNodeHash() {
    uint64_t chip = ESP.getEfuseMac();
    uint8_t buf[8];
    for (uint8_t i = 0; i < 8; ++i) {
      buf[i] = static_cast<uint8_t>((chip >> (i * 8)) & 0xFF);
    }
    uint32_t h = fnv1a(buf, sizeof(buf));
    return h ? h : FALLBACK_NODE_HASH;
  }

  static void buildNodeId(uint32_t nodeHash, char* out, size_t outSize) {
    int written = snprintf(out, outSize, "N-%08lX", static_cast<unsigned long>(nodeHash));
    if (written < 0 || static_cast<size_t>(written) >= outSize) {
      if (outSize > 0) out[outSize - 1] = '\0';
    }
  }

  static bool isAsciiDigits(const String& s) {
    if (s.isEmpty()) return false;
    for (size_t i = 0; i < s.length(); ++i) {
      if (!isDigit(s.charAt(i))) return false;
    }
    return true;
  }

  static bool isValidUsername(const char* username) {
    size_t len = strlen(username);
    return len >= MIN_USERNAME_LENGTH && len < MAX_USERNAME_LENGTH;
  }

  static bool isValidMac(const char* mac) {
    size_t len = strlen(mac);
    return len >= MIN_MAC_LENGTH && len < MAX_MAC_LENGTH;
  }

  void chooseDisplayName() {
    WiFi.mode(WIFI_MODE_APSTA);
    int found = WiFi.scanNetworks(false, true);
    bool used[MAX_DISPLAY_NAME_SUFFIX + 1];
    memset(used, 0, sizeof(used));

    for (int i = 0; i < found; ++i) {
      String ssid = WiFi.SSID(i);
      if (!ssid.startsWith(NODE_NAME_PREFIX)) continue;
      String suffix = ssid.substring(strlen(NODE_NAME_PREFIX));
      if (!isAsciiDigits(suffix)) continue;
      int num = suffix.toInt();
      if (num >= 1 && num <= MAX_DISPLAY_NAME_SUFFIX) used[num] = true;
    }

    int selected = 1;
    while (selected <= MAX_DISPLAY_NAME_SUFFIX && used[selected]) {
      ++selected;
    }
    if (selected > MAX_DISPLAY_NAME_SUFFIX) {
      selected = random(1, MAX_DISPLAY_NAME_SUFFIX + 1);
    }

    int written = snprintf(displayName_, sizeof(displayName_), "%s%d", NODE_NAME_PREFIX, selected);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(displayName_)) {
      copySafe(displayName_, sizeof(displayName_), "Lora-0");
    }
    WiFi.scanDelete();
  }

  void setupWiFiAp() {
    IPAddress localIp(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(localIp, gateway, subnet);
    WiFi.softAP(displayName_, nullptr, AP_CHANNEL, false, AP_MAX_CLIENTS);
  }

  void setupHttp() {
    web_.on("/api/connect", HTTP_POST, [this]() { handleApiConnect(); });
    web_.on("/api/clients", HTTP_GET, [this]() { handleApiClients(); });
    web_.on("/api/send", HTTP_POST, [this]() { handleApiSend(); });
    web_.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
    web_.on("/api/disconnect", HTTP_POST, [this]() { handleApiDisconnect(); });

    web_.onNotFound([this]() {
      StaticJsonDocument<128> doc;
      doc["status"] = "error";
      doc["code"] = "NOT_FOUND";
      sendJson(404, doc);
    });

    web_.begin();
  }

  void setupWebSocket() {
    ws_.begin();
    ws_.onEvent([this](uint8_t clientNum, WStype_t type, uint8_t* payload, size_t length) {
      onWsEvent(clientNum, type, payload, length);
    });
  }

  void setupLoRa() {
    LoRa.setPins(LORA_PIN_CS, LORA_PIN_RST, LORA_PIN_IRQ);
    if (!LoRa.begin(LORA_FREQUENCY)) {
      Serial.println("[LORA] init failed");
      return;
    }
    LoRa.setSignalBandwidth(LORA_BW);
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setCodingRate4(LORA_CR);
    LoRa.setTxPower(LORA_TX_POWER);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.enableCrc();
  }

  void sendJson(int statusCode, const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    web_.send(statusCode, "application/json", out);
  }

  bool parseBodyJson(JsonDocument& doc) {
    String body = web_.arg("plain");
    if (body.isEmpty()) return false;
    DeserializationError err = deserializeJson(doc, body);
    return !err;
  }

  SessionRecord* findSessionByToken(const char* token) {
    if (!token || token[0] == '\0') return nullptr;
    for (auto& s : sessions_) {
      if (s.used && strcmp(s.token, token) == 0) return &s;
    }
    return nullptr;
  }

  SessionRecord* findSessionByUsername(const char* username) {
    if (!username || username[0] == '\0') return nullptr;
    for (auto& s : sessions_) {
      if (s.used && strcmp(s.username, username) == 0) return &s;
    }
    return nullptr;
  }

  ClientRecord* findClientByUsername(const char* username) {
    if (!username || username[0] == '\0') return nullptr;
    for (auto& c : clients_) {
      if (c.used && strcmp(c.username, username) == 0) return &c;
    }
    return nullptr;
  }

  ClientRecord* allocClient() {
    for (auto& c : clients_) {
      if (!c.used) {
        memset(&c, 0, sizeof(c));
        c.used = true;
        return &c;
      }
    }
    return nullptr;
  }

  SessionRecord* allocSession() {
    for (auto& s : sessions_) {
      if (!s.used) {
        memset(&s, 0, sizeof(s));
        s.used = true;
        return &s;
      }
    }
    return nullptr;
  }

  PendingMessage* allocPending() {
    for (auto& m : pending_) {
      if (!m.used) {
        memset(&m, 0, sizeof(m));
        m.used = true;
        return &m;
      }
    }
    return nullptr;
  }

  uint8_t countLocalOnlineClients() const {
    uint8_t c = 0;
    for (const auto& item : clients_) {
      if (item.used && item.isLocal && item.isOnline) ++c;
    }
    return c;
  }

  uint8_t countKnownClients() const {
    uint8_t c = 0;
    for (const auto& item : clients_) {
      if (item.used) ++c;
    }
    return c;
  }

  uint8_t countNeighbors() const {
    uint8_t c = 0;
    for (const auto& n : neighbors_) {
      if (n.used) ++c;
    }
    return c;
  }

  uint8_t countSeenPackets() const {
    uint8_t c = 0;
    for (const auto& s : seenPackets_) {
      if (s.used) ++c;
    }
    return c;
  }

  uint8_t countTxQueue() const {
    uint8_t c = 0;
    for (const auto& q : txQueue_) {
      if (q.used) ++c;
    }
    return c;
  }

  bool isUsernameTakenKnown(const char* username, uint32_t requesterHash) {
    ClientRecord* c = findClientByUsername(username);
    if (!c) return false;
    if (c->homeNodeHash == requesterHash) return false;
    return true;
  }

  void randomHex(char* out, size_t chars) {
    static const char* h = "0123456789ABCDEF";
    for (size_t i = 0; i < chars; ++i) {
      out[i] = h[random(0, 16)];
    }
    out[chars] = '\0';
  }

  void handleApiConnect() {
    StaticJsonDocument<512> req;
    if (!parseBodyJson(req)) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "BAD_JSON";
      sendJson(400, err);
      return;
    }

    const char* username = req["username"] | "";
    const char* mac = req["mac"] | "";

    if (!isValidUsername(username) || !isValidMac(mac)) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "BAD_REQUEST";
      sendJson(400, err);
      return;
    }

    if (countLocalOnlineClients() >= MAX_LOCAL_CLIENTS) {
      ClientRecord* existing = findClientByUsername(username);
      if (!(existing && existing->isLocal && existing->isOnline)) {
        StaticJsonDocument<128> err;
        err["status"] = "error";
        err["code"] = "LOCAL_CAPACITY_REACHED";
        sendJson(503, err);
        return;
      }
    }

    if (isUsernameTakenKnown(username, nodeHash_)) {
      StaticJsonDocument<192> err;
      err["status"] = "error";
      err["code"] = "USERNAME_TAKEN";
      err["message"] = "Username is already taken";
      sendJson(409, err);
      return;
    }

    ClientRecord* client = findClientByUsername(username);
    if (!client) client = allocClient();
    if (!client) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "REGISTRY_FULL";
      sendJson(503, err);
      return;
    }

    SessionRecord* session = findSessionByUsername(username);
    if (!session) session = allocSession();
    if (!session) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "SESSION_LIMIT";
      sendJson(503, err);
      return;
    }

    uint32_t now = millis();
    memset(client, 0, sizeof(*client));
    client->used = true;
    copySafe(client->username, sizeof(client->username), username);
    copySafe(client->mac, sizeof(client->mac), mac);
    client->homeNodeHash = nodeHash_;
    client->isOnline = true;
    client->isLocal = true;
    client->lastSeenMs = now;
    client->updatedAtMs = now;

    memset(session, 0, sizeof(*session));
    session->used = true;
    copySafe(session->username, sizeof(session->username), username);
    copySafe(session->clientMac, sizeof(session->clientMac), mac);
    randomHex(session->token, SESSION_TOKEN_LENGTH);
    session->createdAtMs = now;
    session->lastActivityMs = now;
    session->lastPongMs = now;
    session->wsConnected = false;

    client->sessionHash = hashText(session->token);

    StaticJsonDocument<384> resp;
    resp["status"] = "ok";
    resp["username"] = username;
    resp["nodeId"] = nodeId_;
    resp["displayName"] = displayName_;
    resp["sessionToken"] = session->token;
    resp["sessionTtlSec"] = SESSION_IDLE_TIMEOUT_MS / 1000;
    sendJson(200, resp);

    publishClientUpdate(*client);
    broadcastNetworkEvent("USER_JOINED", client->username, nodeId_);
  }

  bool authorizeFromHeaders(SessionRecord*& session) {
    String username = web_.header("X-Username");
    String token = web_.header("X-Session-Token");
    if (username.isEmpty() || token.isEmpty()) return false;
    session = findSessionByToken(token.c_str());
    if (!session) return false;
    if (strcmp(session->username, username.c_str()) != 0) return false;
    session->lastActivityMs = millis();
    return true;
  }

  void handleApiClients() {
    SessionRecord* session = nullptr;
    if (!authorizeFromHeaders(session)) {
      StaticJsonDocument<160> err;
      err["status"] = "error";
      err["code"] = "SESSION_INVALID";
      sendJson(401, err);
      return;
    }

    StaticJsonDocument<4096> resp;
    resp["status"] = "ok";
    resp["totalClients"] = countKnownClients();
    JsonArray arr = resp.createNestedArray("clients");

    for (const auto& c : clients_) {
      if (!c.used) continue;
      JsonObject x = arr.createNestedObject();
      x["username"] = c.username;

      char homeNode[16];
      buildNodeId(c.homeNodeHash, homeNode, sizeof(homeNode));
      x["homeNode"] = homeNode;
      x["displayNode"] = (c.homeNodeHash == nodeHash_) ? displayName_ : "remote";
      x["isOnline"] = c.isOnline;
      x["isLocal"] = c.isLocal;
    }

    sendJson(200, resp);
  }

  void handleApiSend() {
    StaticJsonDocument<512> req;
    if (!parseBodyJson(req)) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "BAD_JSON";
      sendJson(400, err);
      return;
    }

    const char* token = req["sessionToken"] | "";
    const char* to = req["to"] | "";
    const char* content = req["content"] | "";

    if (strlen(token) != SESSION_TOKEN_LENGTH) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "SESSION_INVALID";
      sendJson(401, err);
      return;
    }

    SessionRecord* session = findSessionByToken(token);
    if (!session) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "SESSION_INVALID";
      sendJson(401, err);
      return;
    }
    session->lastActivityMs = millis();

    if (!isValidUsername(to) || strlen(content) == 0 || strlen(content) > MAX_MESSAGE_TEXT) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "PAYLOAD_TOO_LARGE";
      sendJson(400, err);
      return;
    }

    ClientRecord* dst = findClientByUsername(to);
    if (!dst) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "USER_NOT_FOUND";
      sendJson(404, err);
      return;
    }

    PendingMessage* p = allocPending();
    if (!p) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "QUEUE_FULL";
      sendJson(503, err);
      return;
    }

    uint32_t now = millis();
    uint32_t messageSeq = ++packetSeq_;
    snprintf(p->messageId, sizeof(p->messageId), "M-%08lX-%08lX", static_cast<unsigned long>(nodeHash_), static_cast<unsigned long>(messageSeq));
    copySafe(p->fromUsername, sizeof(p->fromUsername), session->username);
    copySafe(p->toUsername, sizeof(p->toUsername), to);
    copySafe(p->content, sizeof(p->content), content);
    p->dstNodeHash = dst->homeNodeHash;
    p->originNodeHash = nodeHash_;
    p->createdAtMs = now;
    p->lastTryMs = 0;
    p->retryCount = 0;
    p->status = MSG_PENDING;
    p->waitingForClientAck = true;

    if (dst->homeNodeHash == nodeHash_) {
      deliverToLocalClient(*p);
      p->status = MSG_FORWARDED;
      p->lastTryMs = now;
    } else {
      enqueueMeshMessage(*p, true, DEFAULT_TTL, 2, random(50, 250));
      p->status = MSG_FORWARDED;
      p->lastTryMs = now;
    }

    StaticJsonDocument<256> resp;
    resp["status"] = "accepted";
    resp["messageId"] = p->messageId;
    resp["deliveryStatus"] = "pending";
    sendJson(202, resp);
  }

  void handleApiStatus() {
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < minFreeHeap_) minFreeHeap_ = freeHeap;

    StaticJsonDocument<512> resp;
    resp["nodeId"] = nodeId_;
    resp["displayName"] = displayName_;
    resp["uptimeMs"] = millis() - startedAtMs_;
    resp["localClients"] = countLocalOnlineClients();
    resp["knownClients"] = countKnownClients();
    resp["neighbors"] = countNeighbors();
    resp["freeHeap"] = freeHeap;
    resp["minFreeHeap"] = minFreeHeap_;
    uint32_t freePsram = psramFound() ? ESP.getFreePsram() : 0;
    resp["freePsram"] = freePsram;
    resp["loraQueue"] = countTxQueue();
    resp["seenPackets"] = countSeenPackets();
    sendJson(200, resp);
  }

  void handleApiDisconnect() {
    StaticJsonDocument<192> req;
    if (!parseBodyJson(req)) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "BAD_JSON";
      sendJson(400, err);
      return;
    }

    const char* token = req["sessionToken"] | "";
    SessionRecord* session = findSessionByToken(token);
    if (!session) {
      StaticJsonDocument<128> err;
      err["status"] = "error";
      err["code"] = "SESSION_INVALID";
      sendJson(401, err);
      return;
    }

    ClientRecord* client = findClientByUsername(session->username);
    if (client) {
      client->isOnline = false;
      client->lastSeenMs = millis();
      client->updatedAtMs = millis();
      publishClientUpdate(*client);
      broadcastNetworkEvent("USER_LEFT", client->username, nodeId_);
    }

    clearWsForUser(session->username);
    memset(session, 0, sizeof(*session));

    StaticJsonDocument<64> resp;
    resp["status"] = "ok";
    sendJson(200, resp);
  }

  void onWsEvent(uint8_t clientNum, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
      case WStype_CONNECTED:
        handleWsConnected(clientNum, reinterpret_cast<const char*>(payload));
        break;
      case WStype_TEXT:
        handleWsText(clientNum, payload, length);
        break;
      case WStype_DISCONNECTED:
        handleWsDisconnected(clientNum);
        break;
      default:
        break;
    }
  }

  const char* wsUserByClientNum(uint8_t clientNum) {
    for (auto& b : wsBindings_) {
      if (b.used && b.clientNum == clientNum) return b.username;
    }
    return nullptr;
  }

  bool bindWsUser(uint8_t clientNum, const char* username) {
    for (auto& b : wsBindings_) {
      if (b.used && b.clientNum == clientNum) {
        copySafe(b.username, sizeof(b.username), username);
        return true;
      }
    }
    for (auto& b : wsBindings_) {
      if (!b.used) {
        b.used = true;
        b.clientNum = clientNum;
        copySafe(b.username, sizeof(b.username), username);
        return true;
      }
    }
    return false;
  }

  void unbindWsClient(uint8_t clientNum) {
    for (auto& b : wsBindings_) {
      if (b.used && b.clientNum == clientNum) {
        memset(&b, 0, sizeof(b));
      }
    }
  }

  void clearWsForUser(const char* username) {
    for (auto& b : wsBindings_) {
      if (b.used && strcmp(b.username, username) == 0) {
        ws_.disconnect(b.clientNum);
        memset(&b, 0, sizeof(b));
      }
    }
  }

  void handleWsConnected(uint8_t clientNum, const char* pathRaw) {
    String path = pathRaw ? String(pathRaw) : String();
    int q = path.indexOf('?');
    if (q < 0) {
      ws_.disconnect(clientNum);
      return;
    }

    String query = path.substring(q + 1);
    String username;
    String token;

    int pos = 0;
    while (pos < query.length()) {
      int amp = query.indexOf('&', pos);
      if (amp < 0) amp = query.length();
      String part = query.substring(pos, amp);
      int eq = part.indexOf('=');
      if (eq > 0) {
        String key = part.substring(0, eq);
        String val = part.substring(eq + 1);
        if (key == "username") username = val;
        if (key == "token") token = val;
      }
      pos = amp + 1;
    }

    SessionRecord* session = findSessionByToken(token.c_str());
    if (!session || strcmp(session->username, username.c_str()) != 0) {
      ws_.disconnect(clientNum);
      return;
    }

    session->wsConnected = true;
    session->lastActivityMs = millis();
    session->lastPongMs = millis();

    if (!bindWsUser(clientNum, session->username)) {
      ws_.disconnect(clientNum);
      return;
    }

    StaticJsonDocument<128> hello;
    hello["type"] = "HELLO";
    hello["nodeId"] = nodeId_;
    String out;
    serializeJson(hello, out);
    ws_.sendTXT(clientNum, out);
  }

  void handleWsDisconnected(uint8_t clientNum) {
    const char* username = wsUserByClientNum(clientNum);
    if (username) {
      SessionRecord* s = findSessionByUsername(username);
      if (s) {
        s->wsConnected = false;
        s->lastActivityMs = millis();
      }
    }
    unbindWsClient(clientNum);
  }

  void handleWsText(uint8_t clientNum, uint8_t* payload, size_t length) {
    if (length == 0 || length > MAX_WS_MESSAGE_LENGTH) return;

    StaticJsonDocument<512> doc;
    auto err = deserializeJson(doc, payload, length);
    if (err) return;

    const char* username = wsUserByClientNum(clientNum);
    if (!username) return;

    SessionRecord* session = findSessionByUsername(username);
    if (!session) return;
    session->lastActivityMs = millis();

    const char* type = doc["type"] | "";
    if (strcmp(type, "PONG") == 0) {
      session->lastPongMs = millis();
      return;
    }

    if (strcmp(type, "MESSAGE_ACK") == 0) {
      const char* messageId = doc["messageId"] | "";
      if (messageId[0] == '\0') return;
      onClientDeliveryAck(username, messageId);
    }
  }

  void broadcastNetworkEvent(const char* eventName, const char* username, const char* homeNode) {
    StaticJsonDocument<192> doc;
    doc["type"] = "NETWORK_EVENT";
    doc["event"] = eventName;
    doc["username"] = username;
    doc["homeNode"] = homeNode;
    String out;
    serializeJson(doc, out);
    ws_.broadcastTXT(out);
  }

  void deliverToLocalClient(const PendingMessage& msg) {
    uint8_t clientNum = INVALID_WS_CLIENT_NUM;
    for (const auto& b : wsBindings_) {
      if (b.used && strcmp(b.username, msg.toUsername) == 0) {
        clientNum = b.clientNum;
        break;
      }
    }

    if (clientNum == INVALID_WS_CLIENT_NUM) return;

    StaticJsonDocument<512> doc;
    doc["type"] = "MESSAGE_INCOMING";
    doc["messageId"] = msg.messageId;
    doc["from"] = msg.fromUsername;
    doc["content"] = msg.content;
    doc["timestamp"] = millis() / 1000;

    String out;
    serializeJson(doc, out);
    ws_.sendTXT(clientNum, out);
  }

  void notifySenderStatus(const char* sender, const char* messageId, const char* status) {
    for (const auto& b : wsBindings_) {
      if (!b.used || strcmp(b.username, sender) != 0) continue;
      StaticJsonDocument<192> doc;
      doc["type"] = "MESSAGE_STATUS";
      doc["messageId"] = messageId;
      doc["status"] = status;
      String out;
      serializeJson(doc, out);
      ws_.sendTXT(b.clientNum, out);
    }
  }

  void onClientDeliveryAck(const char* receiverUsername, const char* messageId) {
    for (auto& p : pending_) {
      if (!p.used) continue;
      if (strcmp(p.messageId, messageId) != 0) continue;
      if (strcmp(p.toUsername, receiverUsername) != 0) continue;

      p.status = MSG_DELIVERED;
      p.waitingForClientAck = false;

      if (p.originNodeHash == nodeHash_) {
        notifySenderStatus(p.fromUsername, p.messageId, "delivered");
      } else {
        sendDeliveryAckToOrigin(p);
      }
      return;
    }
  }

  void sendNodeHello() {
    StaticJsonDocument<96> doc;
    doc["nodeId"] = nodeId_;
    doc["displayName"] = displayName_;
    uint8_t payload[96];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    sendMeshPacket(NODE_HELLO, BROADCAST_NODE_HASH, payload, len, DEFAULT_TTL, FLAG_BROADCAST, 4, 0);
  }

  void sendHeartbeatIfNeeded() {
    uint32_t now = millis();
    if (now - lastHeartbeatMs_ < HEARTBEAT_INTERVAL_MS) return;
    lastHeartbeatMs_ = now;

    StaticJsonDocument<128> doc;
    doc["nodeId"] = nodeId_;
    doc["displayName"] = displayName_;
    doc["clients"] = countLocalOnlineClients();
    uint8_t payload[128];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    sendMeshPacket(NODE_HEARTBEAT, BROADCAST_NODE_HASH, payload, len, DEFAULT_TTL, FLAG_BROADCAST, 5, 0);
  }

  void publishClientUpdate(const ClientRecord& c) {
    StaticJsonDocument<192> doc;
    doc["username"] = c.username;
    doc["mac"] = c.mac;
    doc["homeNodeHash"] = c.homeNodeHash;
    doc["isOnline"] = c.isOnline;
    doc["lastSeenMs"] = c.lastSeenMs;

    uint8_t payload[192];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    sendMeshPacket(CLIENT_UPDATE, BROADCAST_NODE_HASH, payload, len, DEFAULT_TTL, FLAG_BROADCAST, 3, random(50, 250));
  }

  void sendDeliveryAckToOrigin(const PendingMessage& p) {
    StaticJsonDocument<128> doc;
    doc["messageId"] = p.messageId;
    doc["status"] = "delivered";
    uint8_t payload[128];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    sendMeshPacket(MSG_DELIVERY_ACK, p.originNodeHash, payload, len, DEFAULT_TTL, 0, 1, random(30, 80));
  }

  void enqueueMeshMessage(const PendingMessage& p, bool ackRequired, uint8_t ttl, uint8_t priority, uint16_t jitterMs) {
    StaticJsonDocument<320> doc;
    doc["messageId"] = p.messageId;
    doc["from"] = p.fromUsername;
    doc["to"] = p.toUsername;
    doc["content"] = p.content;
    doc["originNodeHash"] = p.originNodeHash;

    uint8_t payload[320];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    uint8_t flags = ackRequired ? FLAG_ACK_REQUIRED : 0;
    sendMeshPacket(MSG_FORWARD, p.dstNodeHash, payload, len, ttl, flags, priority, jitterMs);
  }

  void sendMeshPacket(uint8_t type, uint32_t dstNodeHash, const uint8_t* payload, size_t payloadLen, uint8_t ttl, uint8_t flags, uint8_t priority, uint16_t jitterMs) {
    if (payloadLen > MAX_MESH_PAYLOAD) return;

    MeshHeader h;
    h.magic = MESH_MAGIC;
    h.version = MESH_VERSION;
    h.type = type;
    h.ttl = ttl;
    h.flags = flags;
    h.originNodeHash = nodeHash_;
    h.srcNodeHash = nodeHash_;
    h.dstNodeHash = dstNodeHash;
    h.packetSeq = ++packetSeq_;

    uint8_t idSeed[12];
    memcpy(idSeed, &h.originNodeHash, 4);
    memcpy(idSeed + 4, &h.packetSeq, 4);
    memcpy(idSeed + 8, &h.type, 1);
    idSeed[9] = h.ttl;
    idSeed[10] = h.flags;
    idSeed[11] = PACKET_ID_SALT_MESH;
    h.packetId = fnv1a(idSeed, sizeof(idSeed));

    enqueueTx(h, payload, payloadLen, priority, millis() + jitterMs);
  }

  void enqueueTx(const MeshHeader& h, const uint8_t* payload, size_t payloadLen, uint8_t priority, uint32_t dueAtMs) {
    for (auto& q : txQueue_) {
      if (!q.used) {
        q.used = true;
        q.header = h;
        q.payloadLen = payloadLen;
        memcpy(q.payload, payload, payloadLen);
        q.priority = priority;
        q.dueAtMs = dueAtMs;
        return;
      }
    }
  }

  static void serializeHeader(const MeshHeader& h, uint8_t* out) {
    out[0] = static_cast<uint8_t>(h.magic & 0xFF);
    out[1] = static_cast<uint8_t>((h.magic >> 8) & 0xFF);
    out[2] = h.version;
    out[3] = h.type;
    out[4] = h.ttl;
    out[5] = h.flags;

    writeU32(out + 6, h.originNodeHash);
    writeU32(out + 10, h.srcNodeHash);
    writeU32(out + 14, h.dstNodeHash);
    writeU32(out + 18, h.packetSeq);
    writeU32(out + 22, h.packetId);
  }

  static bool parseHeader(const uint8_t* data, size_t len, MeshHeader& h) {
    if (len < MESH_HEADER_SIZE) return false;
    h.magic = static_cast<uint16_t>(data[0] | (data[1] << 8));
    h.version = data[2];
    h.type = data[3];
    h.ttl = data[4];
    h.flags = data[5];
    h.originNodeHash = readU32(data + 6);
    h.srcNodeHash = readU32(data + 10);
    h.dstNodeHash = readU32(data + 14);
    h.packetSeq = readU32(data + 18);
    h.packetId = readU32(data + 22);
    return true;
  }

  static void writeU32(uint8_t* out, uint32_t v) {
    out[0] = static_cast<uint8_t>(v & 0xFF);
    out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
  }

  static uint32_t readU32(const uint8_t* in) {
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) |
           (static_cast<uint32_t>(in[3]) << 24);
  }

  bool seenPacketsContains(uint32_t originNodeHash, uint32_t packetId) {
    for (const auto& s : seenPackets_) {
      if (s.used && s.originNodeHash == originNodeHash && s.packetId == packetId) {
        return true;
      }
    }
    return false;
  }

  void seenPacketsAdd(uint32_t originNodeHash, uint32_t packetId) {
    uint32_t now = millis();
    for (auto& s : seenPackets_) {
      if (!s.used) {
        s.used = true;
        s.originNodeHash = originNodeHash;
        s.packetId = packetId;
        s.seenAtMs = now;
        return;
      }
    }

    uint8_t oldest = 0;
    for (uint8_t i = 1; i < MAX_SEEN_PACKETS; ++i) {
      if (seenPackets_[i].seenAtMs < seenPackets_[oldest].seenAtMs) oldest = i;
    }
    seenPackets_[oldest].originNodeHash = originNodeHash;
    seenPackets_[oldest].packetId = packetId;
    seenPackets_[oldest].seenAtMs = now;
    seenPackets_[oldest].used = true;
  }

  void cleanupSeenPackets() {
    uint32_t now = millis();
    for (auto& s : seenPackets_) {
      if (s.used && now - s.seenAtMs > SEEN_PACKET_TTL_MS) {
        memset(&s, 0, sizeof(s));
      }
    }
  }

  NeighborRecord* findNeighbor(uint32_t nodeHash) {
    for (auto& n : neighbors_) {
      if (n.used && n.nodeHash == nodeHash) return &n;
    }
    return nullptr;
  }

  RouteRecord* findRoute(uint32_t dstNodeHash) {
    for (auto& r : routes_) {
      if (r.used && r.dstNodeHash == dstNodeHash) return &r;
    }
    return nullptr;
  }

  void updateNeighbor(uint32_t srcNodeHash, int16_t rssi = 0, float snr = 0) {
    NeighborRecord* n = findNeighbor(srcNodeHash);
    if (!n) {
      for (auto& item : neighbors_) {
        if (!item.used) {
          n = &item;
          memset(n, 0, sizeof(*n));
          n->used = true;
          n->nodeHash = srcNodeHash;
          break;
        }
      }
    }

    if (n) {
      n->lastSeenMs = millis();
      n->lastRssi = rssi;
      n->lastSnr = snr;
      n->missedHeartbeats = 0;
    }
  }

  void learnRoute(uint32_t originNodeHash, uint32_t srcNodeHash, int16_t rssi = 0) {
    if (originNodeHash == nodeHash_ || originNodeHash == srcNodeHash) return;
    RouteRecord* r = findRoute(originNodeHash);
    if (!r) {
      for (auto& item : routes_) {
        if (!item.used) {
          r = &item;
          memset(r, 0, sizeof(*r));
          r->used = true;
          r->dstNodeHash = originNodeHash;
          break;
        }
      }
    }

    if (r) {
      r->nextHopHash = srcNodeHash;
      r->hopCount = 1;
      r->rssi = rssi;
      r->updatedAtMs = millis();
    }
  }

  bool shouldRelay(const MeshHeader& h) {
    if (h.ttl == 0) return false;
    if (h.originNodeHash == nodeHash_) return false;

    if (h.flags & FLAG_BROADCAST) return true;
    if (h.dstNodeHash == nodeHash_) return false;

    return true;
  }

  void relayPacket(const MeshHeader& h, const uint8_t* payload, size_t payloadLen) {
    MeshHeader relay = h;
    relay.ttl = h.ttl - 1;
    relay.srcNodeHash = nodeHash_;
    enqueueTx(relay, payload, payloadLen, 3, millis() + random(50, 250));
  }

  void handleLoRaRx() {
    int packetSize = LoRa.parsePacket();
    if (packetSize <= 0) return;
    if (packetSize > MAX_LORA_PACKET_SIZE) {
      while (LoRa.available()) LoRa.read();
      return;
    }

    uint8_t data[MAX_LORA_PACKET_SIZE];
    size_t len = 0;
    while (LoRa.available() && len < sizeof(data)) {
      data[len++] = static_cast<uint8_t>(LoRa.read());
    }

    MeshHeader h;
    if (!parseHeader(data, len, h)) return;
    if (h.magic != MESH_MAGIC || h.version != MESH_VERSION) return;

    if (h.originNodeHash == nodeHash_) return;
    if (seenPacketsContains(h.originNodeHash, h.packetId)) return;

    seenPacketsAdd(h.originNodeHash, h.packetId);
    updateNeighbor(h.srcNodeHash, LoRa.packetRssi(), LoRa.packetSnr());
    learnRoute(h.originNodeHash, h.srcNodeHash, LoRa.packetRssi());

    const uint8_t* payload = data + MESH_HEADER_SIZE;
    size_t payloadLen = len - MESH_HEADER_SIZE;
    processMeshPayload(h, payload, payloadLen);

    if (shouldRelay(h)) {
      relayPacket(h, payload, payloadLen);
    }
  }

  void processMeshPayload(const MeshHeader& h, const uint8_t* payload, size_t payloadLen) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, payload, payloadLen)) return;

    switch (h.type) {
      case NODE_HELLO:
      case NODE_HEARTBEAT:
        break;

      case CLIENT_UPDATE: {
        const char* username = doc["username"] | "";
        const char* mac = doc["mac"] | "";
        uint32_t homeNodeHash = doc["homeNodeHash"] | 0;
        bool isOnline = doc["isOnline"] | false;
        uint32_t lastSeenMs = doc["lastSeenMs"] | 0;

        if (username[0] == '\0' || homeNodeHash == 0) break;

        ClientRecord* c = findClientByUsername(username);
        if (!c) c = allocClient();
        if (!c) break;

        if (c->used && c->username[0] != '\0' && c->homeNodeHash != 0 && c->homeNodeHash != homeNodeHash) {
          if (c->updatedAtMs <= lastSeenMs) {
            memset(c, 0, sizeof(*c));
            c->used = true;
          } else {
            break;
          }
        }

        copySafe(c->username, sizeof(c->username), username);
        copySafe(c->mac, sizeof(c->mac), mac);
        c->homeNodeHash = homeNodeHash;
        c->isOnline = isOnline;
        c->isLocal = (homeNodeHash == nodeHash_);
        c->lastSeenMs = lastSeenMs;
        c->updatedAtMs = millis();
      } break;

      case MSG_FORWARD: {
        const char* messageId = doc["messageId"] | "";
        const char* from = doc["from"] | "";
        const char* to = doc["to"] | "";
        const char* content = doc["content"] | "";
        uint32_t originNodeHash = doc["originNodeHash"] | h.originNodeHash;

        if (messageId[0] == '\0' || from[0] == '\0' || to[0] == '\0') break;

        ClientRecord* dst = findClientByUsername(to);
        if (!dst) break;

        if (dst->homeNodeHash == nodeHash_) {
          PendingMessage* p = allocPending();
          if (!p) break;

          copySafe(p->messageId, sizeof(p->messageId), messageId);
          copySafe(p->fromUsername, sizeof(p->fromUsername), from);
          copySafe(p->toUsername, sizeof(p->toUsername), to);
          copySafe(p->content, sizeof(p->content), content);
          p->dstNodeHash = nodeHash_;
          p->originNodeHash = originNodeHash;
          p->createdAtMs = millis();
          p->lastTryMs = millis();
          p->retryCount = 0;
          p->status = MSG_FORWARDED;
          p->waitingForClientAck = true;

          deliverToLocalClient(*p);
          sendNodeAck(h, messageId);
        }
      } break;

      case MSG_NODE_ACK:
        break;

      case MSG_DELIVERY_ACK: {
        const char* messageId = doc["messageId"] | "";
        if (messageId[0] == '\0') break;
        for (auto& p : pending_) {
          if (!p.used) continue;
          if (strcmp(p.messageId, messageId) == 0) {
            p.status = MSG_DELIVERED;
            p.waitingForClientAck = false;
            notifySenderStatus(p.fromUsername, p.messageId, "delivered");
            break;
          }
        }
      } break;

      case MSG_DELIVERY_FAIL: {
        const char* messageId = doc["messageId"] | "";
        if (messageId[0] == '\0') break;
        for (auto& p : pending_) {
          if (!p.used) continue;
          if (strcmp(p.messageId, messageId) == 0) {
            p.status = MSG_FAILED;
            p.waitingForClientAck = false;
            notifySenderStatus(p.fromUsername, p.messageId, "failed");
            break;
          }
        }
      } break;

      default:
        break;
    }
  }

  void sendNodeAck(const MeshHeader& inbound, const char* messageId) {
    StaticJsonDocument<96> doc;
    doc["messageId"] = messageId;
    uint8_t payload[96];
    size_t len = serializeJson(doc, payload, sizeof(payload));

    MeshHeader h;
    h.magic = MESH_MAGIC;
    h.version = MESH_VERSION;
    h.type = MSG_NODE_ACK;
    h.ttl = DEFAULT_TTL;
    h.flags = 0;
    h.originNodeHash = nodeHash_;
    h.srcNodeHash = nodeHash_;
    h.dstNodeHash = inbound.srcNodeHash;
    h.packetSeq = ++packetSeq_;

    uint8_t idSeed[12];
    memcpy(idSeed, &h.originNodeHash, 4);
    memcpy(idSeed + 4, &h.packetSeq, 4);
    idSeed[8] = h.type;
    idSeed[9] = h.ttl;
    idSeed[10] = h.flags;
    idSeed[11] = PACKET_ID_SALT_ACK;
    h.packetId = fnv1a(idSeed, sizeof(idSeed));

    enqueueTx(h, payload, len, 1, millis() + random(20, 80));
  }

  void processLoRaTxQueue() {
    uint32_t now = millis();

    int idx = -1;
    uint8_t bestPrio = INVALID_PRIORITY;
    uint32_t bestDue = UINT32_MAX;

    for (int i = 0; i < MAX_TX_QUEUE; ++i) {
      const auto& q = txQueue_[i];
      if (!q.used || q.dueAtMs > now) continue;
      if (q.priority < bestPrio || (q.priority == bestPrio && q.dueAtMs < bestDue)) {
        bestPrio = q.priority;
        bestDue = q.dueAtMs;
        idx = i;
      }
    }

    if (idx < 0) return;

    TxQueueItem item = txQueue_[idx];
    memset(&txQueue_[idx], 0, sizeof(txQueue_[idx]));

    uint8_t buffer[MESH_HEADER_SIZE + MAX_MESH_PAYLOAD];
    serializeHeader(item.header, buffer);
    memcpy(buffer + MESH_HEADER_SIZE, item.payload, item.payloadLen);

    LoRa.beginPacket();
    LoRa.write(buffer, MESH_HEADER_SIZE + item.payloadLen);
    LoRa.endPacket(true);
  }

  void processPendingMessages() {
    uint32_t now = millis();
    for (auto& p : pending_) {
      if (!p.used) continue;
      if (p.status == MSG_DELIVERED || p.status == MSG_FAILED) continue;
      if (!p.waitingForClientAck) continue;
      if (now - p.createdAtMs > MSG_TTL_MS) {
        p.status = MSG_FAILED;
        notifySenderStatus(p.fromUsername, p.messageId, "failed");
        continue;
      }

      if (now - p.lastTryMs < MSG_RETRY_DELAY_MS) continue;

      if (p.retryCount >= MSG_MAX_RETRY) {
        p.status = MSG_FAILED;
        notifySenderStatus(p.fromUsername, p.messageId, "failed");
        sendDeliveryFailToOrigin(p);
        continue;
      }

      ++p.retryCount;
      p.lastTryMs = now;

      if (p.dstNodeHash == nodeHash_) {
        deliverToLocalClient(p);
      } else {
        enqueueMeshMessage(p, true, DEFAULT_TTL, 2, random(50, 250));
      }
    }
  }

  void sendDeliveryFailToOrigin(const PendingMessage& p) {
    if (p.originNodeHash == nodeHash_) return;
    StaticJsonDocument<96> doc;
    doc["messageId"] = p.messageId;
    doc["status"] = "failed";
    uint8_t payload[96];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    sendMeshPacket(MSG_DELIVERY_FAIL, p.originNodeHash, payload, len, DEFAULT_TTL, 0, 1, random(20, 80));
  }

  void cleanupPendingMessages() {
    uint32_t now = millis();
    for (auto& p : pending_) {
      if (!p.used) continue;
      if (p.status == MSG_DELIVERED || p.status == MSG_FAILED || now - p.createdAtMs > MSG_TTL_MS) {
        memset(&p, 0, sizeof(p));
      }
    }
  }

  void cleanupNeighbors() {
    uint32_t now = millis();
    for (auto& n : neighbors_) {
      if (!n.used) continue;
      if (now - n.lastSeenMs > NODE_TIMEOUT_MS) {
        memset(&n, 0, sizeof(n));
      }
    }
  }

  void cleanupSessions() {
    uint32_t now = millis();

    for (auto& s : sessions_) {
      if (!s.used) continue;

      bool wsTimedOut = s.wsConnected && (now - s.lastPongMs > (WS_PING_INTERVAL_MS + WS_PONG_TIMEOUT_MS));
      if (wsTimedOut) {
        s.wsConnected = false;
      }

      if (now - s.lastActivityMs > SESSION_IDLE_TIMEOUT_MS) {
        ClientRecord* c = findClientByUsername(s.username);
        if (c) {
          c->isOnline = false;
          c->lastSeenMs = now;
          c->updatedAtMs = now;
          publishClientUpdate(*c);
          broadcastNetworkEvent("USER_LEFT", c->username, nodeId_);
        }
        clearWsForUser(s.username);
        memset(&s, 0, sizeof(s));
        continue;
      }

      if (!s.wsConnected) {
        ClientRecord* c = findClientByUsername(s.username);
        if (c && c->isOnline && now - s.lastActivityMs > CLIENT_OFFLINE_GRACE_MS) {
          c->isOnline = false;
          c->lastSeenMs = now;
          c->updatedAtMs = now;
          publishClientUpdate(*c);
        }
      }
    }
  }

  void pingWsIfNeeded() {
    uint32_t now = millis();
    if (now - lastWsPingMs_ < WS_PING_INTERVAL_MS) return;
    lastWsPingMs_ = now;

    StaticJsonDocument<96> doc;
    doc["type"] = "PING";
    doc["t"] = now / 1000;
    String out;
    serializeJson(doc, out);
    ws_.broadcastTXT(out);
  }
};

RadioGateNode gNode;

void setup() {
  gNode.begin();
}

void loop() {
  gNode.loop();
}
