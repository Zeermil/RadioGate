# ПРОМПТ: Прошивка LoRa Mesh Ноды (LilyGo T3 v1.6.1)

---

## Задача

Напиши полную прошивку на C++ (Arduino / PlatformIO) для устройства **LilyGo T3 v1.6.1** (ESP32 + SX1276 LoRa), которое является **нодой децентрализованной mesh-сети** с WiFi-точкой доступа для клиентов (Android-приложений).

---

## Полное описание системы

### Что делает нода

Нода выполняет три роли одновременно:
1. **WiFi точка доступа (AP)** — клиенты (Android телефоны) подключаются к открытой WiFi сети ноды.
2. **HTTP + WebSocket сервер** — предоставляет REST API и WebSocket интерфейс для клиентов.
3. **LoRa mesh участник** — общается с другими нодами по LoRa радиоканалу, формируя децентрализованную mesh сеть.

---

## Часть 1 — Инициализация и самоименование

### Требования к загрузке

При включении нода должна:

1. **Не стартовать сразу.** Сгенерировать случайную задержку от 500 мс до 3000 мс (`random(500, 3000)`). Это предотвращает коллизии имён при одновременном включении нескольких нод.

2. **Сканировать WiFi эфир** на наличие SSID, начинающихся с префикса `"Lora-"`. Использовать `WiFi.scanNetworks()` в режиме синхронного сканирования. Из найденных SSID извлечь максимальный номер (например, если есть `Lora-1` и `Lora-3`, максимум = 3). Если ни одного SSID `"Lora-*"` не найдено — максимум = 0.

3. **Присвоить себе имя** `"Lora-{MAX+1}"`. Например: найдено `Lora-1`, `Lora-3` → имя ноды = `"Lora-4"`.

4. **Поднять WiFi AP** с именем сети равным имени ноды, без пароля, на канале 6, максимум 8 клиентов.

5. **Запустить HTTP сервер** на порту 80.

6. **Запустить WebSocket сервер** на порту 81.

7. **Инициализировать LoRa** (SX1276) с параметрами: 868 MHz, SF9, BW 125 kHz, CR 4/5, мощность 17 dBm.

8. **Разослать NODE_HELLO broadcast** пакет в LoRa сеть, чтобы оповестить соседей о своём появлении.

9. **Запросить синхронизацию** — отправить `NODE_SYNC_REQUEST` broadcast, чтобы получить от соседей актуальный реестр клиентов.

### Пины LilyGo T3 v1.6.1 (SX1276)
```cpp
#define LORA_SCK   5
#define LORA_MISO  19
#define LORA_MOSI  27
#define LORA_SS    18
#define LORA_RST   14
#define LORA_DIO0  26
```

---

## Часть 2 — Структуры данных в памяти

Все данные хранятся в RAM (использовать PSRAM через `ps_malloc` если доступен). Файловая система не используется для пользовательских данных.

### 2.1 ClientRecord — запись о клиенте

```cpp
struct ClientRecord {
    String   username;       // уникальный ник (регистрозависимый)
    String   macAddress;     // MAC-адрес телефона (формат: "AA:BB:CC:DD:EE:FF")
    String   homeNodeId;     // ID ноды, к которой подключён ("Lora-1")
    bool     isOnline;       // true если сейчас активен
    uint32_t lastSeen;       // millis() последней активности
};
```

Максимум: 50 записей (`MAX_CLIENTS = 50`).

Хранить в глобальном `std::vector<ClientRecord> clientRegistry`.

### 2.2 PendingMessage — очередь сообщений

```cpp
enum MessageStatus { MSG_PENDING, MSG_FORWARDED, MSG_DELIVERED, MSG_FAILED };

struct PendingMessage {
    String        messageId;      // уникальный ID, генерирует нода (если не пришёл от клиента)
    String        fromUsername;
    String        toUsername;
    String        content;
    uint32_t      createdAt;      // millis()
    uint32_t      lastRetryAt;    // millis() последней попытки
    uint8_t       retryCount;     // число попыток отправки
    MessageStatus status;
};
```

Максимум: 50 записей (`MAX_PENDING_MSGS = 50`).
TTL сообщения: 300 секунд с момента создания.
Максимум попыток: 3 (`MSG_MAX_RETRY = 3`).
Интервал повтора: 10 секунд (`MSG_RETRY_DELAY = 10000` мс).

Хранить в `std::vector<PendingMessage> pendingMessages`.

### 2.3 NodeRecord — известные ноды

```cpp
struct NodeRecord {
    String   nodeId;           // "Lora-1", "Lora-2" ...
    uint32_t lastHeartbeat;    // millis() последнего сигнала жизни
    int8_t   rssi;             // сила сигнала последнего пакета
    bool     isAlive;          // true если не протухло
};
```

Нода считается мёртвой если не получен `NODE_HEARTBEAT` за 90 секунд (3 пропущенных heartbeat по 30 сек).

### 2.4 Session — сессия клиента

```cpp
struct Session {
    String   username;
    String   macAddress;
    String   token;            // 6 байт random hex, например "a1b2c3d4e5f6"
    uint32_t createdAt;
    uint32_t lastActivity;
    AsyncWebSocketClient* wsClient; // указатель на WebSocket клиент (nullptr если нет)
};
```

Сессия инвалидируется при отсутствии активности > 600 секунд (10 минут).

---

## Часть 3 — HTTP REST API

Использовать библиотеку `ESPAsyncWebServer`. Все endpoints на порту 80.

Все запросы и ответы в формате `application/json`.

CORS заголовки: добавить `Access-Control-Allow-Origin: *` ко всем ответам.

### Endpoint 1: `POST /api/connect`

Регистрация клиента. Не требует авторизации.

**Входящий JSON:**
```json
{ "username": "Alice", "mac": "AA:BB:CC:DD:EE:FF" }
```

**Логика:**
1. Распарсить JSON. Если поля отсутствуют → ответить 400.
2. Нормализовать `mac` в верхний регистр.
3. **Проверить, занят ли username**: пройтись по `clientRegistry`, проверить что ни одна запись не имеет такой же `username` (с учётом регистра). Если занят → ответить 409.
4. **Проверить переподключение**: если `mac` уже есть в реестре → обновить `homeNodeId` на текущую ноду, `isOnline = true`, создать новую сессию (старый токен аннулируется). Ответить 200.
5. Если новый клиент: создать `ClientRecord`, добавить в `clientRegistry`. Создать `Session` с токеном (6 random bytes hex). Ответить 200.
6. Разослать `CLIENT_UPDATE/ADD` по LoRa (broadcast или всем известным нодам).

**Ответ 200:**
```json
{ "status": "ok", "username": "Alice", "nodeId": "Lora-1", "sessionToken": "a1b2c3d4e5f6", "timestamp": 1712345678 }
```

**Ответ 409:**
```json
{ "status": "error", "code": "USERNAME_TAKEN", "message": "Этот ник уже занят" }
```

---

### Endpoint 2: `GET /api/clients`

Получить полный список клиентов mesh сети.

**Заголовки запроса:** `X-Username`, `X-Session-Token`

Проверить авторизацию. Если токен не валиден → ответить 401.

**Ответ 200:**
```json
{
  "status": "ok",
  "totalClients": 3,
  "clients": [
    { "username": "Alice", "nodeId": "Lora-1", "isOnline": true, "isLocal": true },
    { "username": "Bob",   "nodeId": "Lora-1", "isOnline": true, "isLocal": true },
    { "username": "Carol", "nodeId": "Lora-2", "isOnline": true, "isLocal": false }
  ]
}
```

Поле `isLocal = true` если `homeNodeId` совпадает с `this.nodeId`.

---

### Endpoint 3: `POST /api/send`

Отправить сообщение пользователю.

**Входящий JSON:**
```json
{
  "from": "Alice",
  "to": "Bob",
  "messageId": "msg_a1b2c3d4",
  "content": "Привет!",
  "timestamp": 1712345678,
  "sessionToken": "a1b2c3d4e5f6"
}
```

**Логика:**
1. Проверить `sessionToken`. Если не валиден → 401.
2. Найти получателя в `clientRegistry`. Если не найден → 404.
3. Создать `PendingMessage` и добавить в `pendingMessages`.
4. Если `to.homeNode == this.nodeId`:
   - Найти WS клиент получателя в сессиях.
   - Если WS активен → отправить JSON пакет `MESSAGE_INCOMING` через WebSocket.
   - Статус: `MSG_PENDING` (ждём ACK).
5. Если `to.homeNode != this.nodeId`:
   - Упаковать в LoRa пакет `MSG_FORWARD`, отправить адресно в ноду `to.homeNode`.
   - Статус: `MSG_FORWARDED`.
6. Обновить `lastActivity` сессии отправителя.
7. Ответить 202.

**Ответ 202:**
```json
{ "status": "accepted", "messageId": "msg_a1b2c3d4", "deliveryStatus": "pending" }
```

**Ответ 404:**
```json
{ "status": "error", "code": "USER_NOT_FOUND", "message": "Пользователь не найден" }
```

---

### Endpoint 4: `GET /api/status`

Публичный endpoint, не требует авторизации.

**Ответ 200:**
```json
{
  "nodeId": "Lora-1",
  "uptime": 3600,
  "connectedClients": 3,
  "knownNodes": ["Lora-1", "Lora-2"],
  "totalMeshClients": 6,
  "loraRssi": -87,
  "freeHeap": 124320
}
```

`uptime` в секундах: `millis() / 1000`.
`loraRssi`: последнее RSSI от `LoRa.packetRssi()`.

---

### Endpoint 5: `POST /api/disconnect`

Явное отключение клиента.

**Входящий JSON:**
```json
{ "username": "Alice", "sessionToken": "a1b2c3d4e5f6" }
```

**Логика:**
1. Найти и удалить сессию.
2. Пометить `ClientRecord.isOnline = false`.
3. Разослать `CLIENT_UPDATE/REMOVE` по LoRa.
4. Закрыть WebSocket соединение если открыто.

**Ответ 200:** `{ "status": "ok" }`

---

## Часть 4 — WebSocket сервер

Использовать WebSocket поддержку из `ESPAsyncWebServer` (класс `AsyncWebSocket`).

Путь: `/ws`
Порт: 81

### Авторизация при подключении

При новом WS подключении проверить query параметры `username` и `token`.
Пример: `ws://192.168.4.1:81/ws?username=Alice&token=a1b2c3d4e5f6`

Если токен не найден → закрыть соединение с кодом 4001.
Если токен валиден → привязать `wsClient` к сессии.

### Обработка входящих WS сообщений (от клиента к ноде)

Все сообщения — JSON строки.

**MESSAGE_ACK** — клиент подтверждает получение сообщения:
```json
{ "type": "MESSAGE_ACK", "messageId": "msg_x9y8z7w6", "status": "received" }
```

Логика при получении ACK:
1. Найти `PendingMessage` по `messageId` в `pendingMessages`.
2. Установить `status = MSG_DELIVERED`.
3. Найти WS клиент **отправителя** (из сессий) по `fromUsername`.
4. Отправить отправителю пакет `MESSAGE_DELIVERED`:
```json
{ "type": "MESSAGE_DELIVERED", "messageId": "msg_x9y8z7w6", "to": "Bob", "deliveredAt": 1712345710 }
```
5. Удалить сообщение из очереди.

**PING** — клиент пингует ноду:
```json
{ "type": "PING" }
```
Нода отвечает:
```json
{ "type": "PONG" }
```

### Отправка событий клиентам (от ноды к клиенту)

Нода может в любой момент отправить клиенту:

```json
// Входящее сообщение
{ "type": "MESSAGE_INCOMING", "messageId": "msg_x9y8z7w6", "from": "Bob", "content": "Привет!", "timestamp": 1712345700 }

// Кто-то зашёл/вышел из сети
{ "type": "NETWORK_EVENT", "event": "USER_JOINED", "username": "Dave", "nodeId": "Lora-2" }
{ "type": "NETWORK_EVENT", "event": "USER_LEFT", "username": "Carol" }
```

### Heartbeat WebSocket

Каждые 15 секунд нода отправляет `{ "type": "PING" }` каждому подключённому WS клиенту.
Если `PONG` не получен в течение 5 секунд → считать клиента офлайн:
- `ClientRecord.isOnline = false`
- Разослать `CLIENT_UPDATE/REMOVE` по LoRa
- Закрыть WS соединение

---

## Часть 5 — LoRa Mesh протокол

### 5.1 Формат пакета

Каждый LoRa пакет — бинарная структура с JSON-payload:

```
[MAGIC: 2 bytes: 0x4C 0x4F]
[MSG_TYPE: 1 byte]
[SRC_NODE: 8 bytes, строка с нулевым завершением, например "Lora-1\0\0"]
[DST_NODE: 8 bytes, строка или 0xFF*8 для broadcast]
[PAYLOAD_LEN: 2 bytes, little-endian]
[PAYLOAD: N bytes, JSON строка]
```

Broadcast DST_NODE: все байты 0xFF.

### 5.2 Типы пакетов (MSG_TYPE)

```cpp
enum LoRaMsgType : uint8_t {
    NODE_HELLO           = 0x01,
    NODE_SYNC_REQUEST    = 0x02,
    NODE_SYNC_RESPONSE   = 0x03,
    CLIENT_UPDATE        = 0x04,
    MSG_FORWARD          = 0x05,
    MSG_DELIVERY_ACK     = 0x06,
    MSG_DELIVERY_FAIL    = 0x07,
    NODE_HEARTBEAT       = 0x08,
    NODE_BYE             = 0x09
};
```

### 5.3 Payload каждого типа пакета

**NODE_HELLO** (broadcast, payload пуст или содержит nodeId):
```json
{ "nodeId": "Lora-4" }
```

**NODE_SYNC_REQUEST** (broadcast):
```json
{ "nodeId": "Lora-4", "requestId": "req_001" }
```

**NODE_SYNC_RESPONSE** (адресный, получателю который запросил):
```json
{
  "requestId": "req_001",
  "clients": [
    { "username": "Alice", "mac": "AA:BB:CC:DD:EE:FF", "homeNode": "Lora-1", "isOnline": true },
    { "username": "Bob",   "mac": "BB:CC:DD:EE:FF:AA", "homeNode": "Lora-1", "isOnline": true }
  ]
}
```

Если реестр большой — разбить на несколько пакетов, добавив поля:
```json
{ "fragIndex": 0, "fragTotal": 3, "fragId": 42, "clients": [...] }
```

**CLIENT_UPDATE** (broadcast):
```json
{ "action": "ADD",    "username": "Alice", "mac": "AA:BB:CC:DD:EE:FF", "homeNode": "Lora-1" }
{ "action": "REMOVE", "username": "Alice" }
```

**MSG_FORWARD** (адресный, конкретной ноде):
```json
{
  "messageId": "msg_a1b2c3d4",
  "from": "Alice",
  "to": "Bob",
  "content": "Привет!",
  "timestamp": 1712345678
}
```

**MSG_DELIVERY_ACK** (адресный, обратно ноде-отправителю):
```json
{ "messageId": "msg_a1b2c3d4", "deliveredAt": 1712345710 }
```

**MSG_DELIVERY_FAIL** (адресный):
```json
{ "messageId": "msg_a1b2c3d4", "reason": "USER_OFFLINE" }
```

**NODE_HEARTBEAT** (broadcast):
```json
{ "nodeId": "Lora-1", "clientCount": 3 }
```

**NODE_BYE** (broadcast):
```json
{ "nodeId": "Lora-1" }
```

### 5.4 Обработка входящих LoRa пакетов

Реализовать прерывание или polling в `loop()` для получения LoRa пакетов.

Логика обработки:

```
Получили пакет
  │
  ├─► Парсим заголовок (MAGIC, TYPE, SRC, DST)
  ├─► Если MAGIC != {0x4C, 0x4F} → игнорируем
  ├─► Если DST != broadcast И DST != ourNodeId → игнорируем (не нам)
  ├─► Обновляем NodeRecord для SRC (lastHeartbeat, rssi)
  │
  ├─► NODE_HELLO:
  │     Добавить/обновить NodeRecord для SRC
  │     Ответить NODE_SYNC_RESPONSE адресно (отправить наш реестр)
  │
  ├─► NODE_SYNC_REQUEST:
  │     Собрать весь clientRegistry в пакет(ы) NODE_SYNC_RESPONSE
  │     Отправить адресно в SRC
  │
  ├─► NODE_SYNC_RESPONSE:
  │     Добавить/обновить записи из payload в clientRegistry
  │     (не затирать записи с homeNode == ourNodeId)
  │
  ├─► CLIENT_UPDATE:
  │     action=ADD → добавить/обновить ClientRecord
  │     action=REMOVE → пометить isOnline=false
  │     Разослать NETWORK_EVENT всем подключённым WS клиентам
  │
  ├─► MSG_FORWARD:
  │     Проверить, что to.homeNode == ourNodeId
  │     Найти WS клиент получателя
  │     Отправить MESSAGE_INCOMING через WS
  │     Добавить в pendingMessages (ждём ACK от WS клиента)
  │     (ACK от WS клиента → отправить MSG_DELIVERY_ACK обратно в SRC ноду)
  │
  ├─► MSG_DELIVERY_ACK:
  │     Найти PendingMessage по messageId
  │     Поставить status=DELIVERED
  │     Уведомить WS клиента-отправителя пакетом MESSAGE_DELIVERED
  │
  ├─► MSG_DELIVERY_FAIL:
  │     Найти PendingMessage, поставить status=FAILED
  │     Уведомить WS клиента-отправителя
  │
  └─► NODE_HEARTBEAT:
        Обновить NodeRecord (lastHeartbeat)
```

### 5.5 LoRa Half-duplex и очередь отправки

SX1276 не может TX и RX одновременно. Реализовать очередь исходящих пакетов:

```cpp
struct OutgoingLoRaPacket {
    uint8_t  msgType;
    String   dstNodeId;   // "" для broadcast
    String   payload;
    uint8_t  priority;    // 0=low, 1=normal, 2=high
};

std::queue<OutgoingLoRaPacket> loraOutQueue;
```

В `loop()` после обработки входящего пакета (или таймаут RX) — отправить один пакет из очереди.

---

## Часть 6 — Таймеры и фоновые задачи

Реализовать через `millis()` без блокирующего `delay()` в основном `loop()`.

### 6.1 NODE_HEARTBEAT — каждые 30 секунд

Отправить broadcast LoRa пакет `NODE_HEARTBEAT`.

### 6.2 Очистка мёртвых нод — каждые 30 секунд

Пройтись по `knownNodes`. Если `millis() - lastHeartbeat > 90000` → установить `isAlive = false`. Пометить клиентов этой ноды как офлайн.

### 6.3 Retry несдоставленных сообщений — каждые 10 секунд

Пройтись по `pendingMessages`. Для каждого со статусом `MSG_PENDING` или `MSG_FORWARDED`:
- Если `retryCount >= MAX_RETRY` → установить `MSG_FAILED`, уведомить отправителя.
- Если `millis() - lastRetryAt > MSG_RETRY_DELAY` → повторить попытку доставки, инкрементировать `retryCount`.
- Если `millis() - createdAt > MSG_TTL * 1000` → установить `MSG_FAILED`.

### 6.4 WebSocket Ping-Pong — каждые 15 секунд

Для каждой активной сессии с `wsClient != nullptr`:
- Отправить `{ "type": "PING" }`.
- Установить флаг `pendingPong = true`.
- Через 5 секунд проверить флаг. Если `pendingPong == true` (PONG не пришёл) → клиент оффлайн.

### 6.5 Очистка просроченных сессий — каждые 60 секунд

Для каждой сессии: если `millis() - lastActivity > 600000` → инвалидировать сессию, пометить клиента офлайн, разослать `CLIENT_UPDATE/REMOVE`.

---

## Часть 7 — Вспомогательные функции

### Генерация sessionToken
```cpp
String generateToken() {
    String token = "";
    for (int i = 0; i < 6; i++) {
        token += String(random(0, 256), HEX);
    }
    return token;
}
```

### Генерация messageId (если клиент не передал)
```cpp
String generateMessageId() {
    return "msg_" + String(millis()) + "_" + String(random(0, 10000));
}
```

### Проверка авторизации
```cpp
bool isAuthorized(const String& username, const String& token) {
    for (auto& s : sessions) {
        if (s.username == username && s.token == token) {
            s.lastActivity = millis();
            return true;
        }
    }
    return false;
}
```

### Поиск WS клиента по username
```cpp
AsyncWebSocketClient* findWsClient(const String& username) {
    for (auto& s : sessions) {
        if (s.username == username && s.wsClient != nullptr) {
            return s.wsClient;
        }
    }
    return nullptr;
}
```

---

## Часть 8 — Структура файлов проекта

```
/src
  main.cpp              — setup(), loop(), инициализация
  config.h              — все константы и дефайны
  node_naming.h/.cpp    — WiFi сканирование, присвоение имени
  wifi_ap.h/.cpp        — поднятие AP, мониторинг подключений
  http_server.h/.cpp    — инициализация и обработчики REST API
  websocket_server.h/.cpp — WebSocket логика
  lora_mesh.h/.cpp      — LoRa пакеты, отправка/приём, парсинг
  client_registry.h/.cpp — управление реестром клиентов
  session_manager.h/.cpp — сессии, токены
  message_queue.h/.cpp  — очередь сообщений, retry, TTL
  timers.h/.cpp         — все периодические задачи
  utils.h/.cpp          — вспомогательные функции
```

---

## Часть 9 — platformio.ini

```ini
[env:lilygo-t3]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.partitions = huge_app.csv

lib_deps =
    sandeepmistry/LoRa @ ^0.8.0
    ottowinter/ESPAsyncWebServer-esphome @ ^3.1.0
    ottowinter/AsyncTCP-esphome @ ^2.1.0
    bblanchon/ArduinoJson @ ^7.0.0

build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DCORE_DEBUG_LEVEL=3
```

---

## Часть 10 — Требования к коду

1. **Никаких `delay()`** в основном `loop()`. Только `millis()`-based таймеры.
2. **Обработчики ESPAsyncWebServer** должны быть максимально короткими (не вызывать LoRa TX напрямую из обработчика). Использовать очередь отправки.
3. **Serial.println()** для отладки с тегами: `[BOOT]`, `[HTTP]`, `[WS]`, `[LORA]`, `[REGISTRY]`.
4. **ArduinoJson 7** синтаксис: `JsonDocument doc;` (без шаблонного размера).
5. **Проверять доступность памяти** перед добавлением в векторы. Логировать предупреждение если `heap < 20KB`.
6. **Thread safety**: ESPAsyncWebServer работает в отдельном FreeRTOS таске. Использовать `portMUX_TYPE` мьютекс при доступе к `clientRegistry` и `pendingMessages` из HTTP обработчиков.
7. Весь код комментирован на **русском языке**.
8. **Максимальная длина username**: 20 символов. Разрешённые символы: буквы, цифры, `_`, `-`.
9. **Максимальная длина content** сообщения: 200 символов (LoRa лимит payload).

---

## Ожидаемый результат

Полный рабочий код проекта на C++/Arduino для PlatformIO со всеми описанными файлами. Код должен компилироваться без ошибок для платформы `espressif32` (ESP32). Каждый файл начинается с комментария-заголовка с описанием его назначения.

После основного кода — краткая документация по каждому API endpoint с примерами curl-запросов для тестирования.
