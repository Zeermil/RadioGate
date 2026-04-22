# LoRa Mesh Network — архитектура системы

> Платформа: LilyGo T3 v1.6.1 / ESP32 + SX1276/SX1278  
> Клиент: Android-приложение  
> Локальная связь с клиентами: Wi-Fi AP + HTTP/WebSocket  
> Межнодовая связь: LoRa mesh  
> Версия документа: 2.0

---

## 0. Главная идея

Система строится как аналог Meshtastic, но с другой клиентской моделью:

- одна LoRa-нода обслуживает **несколько Android-клиентов** через собственную открытую Wi-Fi-точку;
- Android-клиент не работает с LoRa напрямую;
- все клиенты подключаются к ближайшей ноде по Wi-Fi;
- ноды пересылают данные друг другу через LoRa;
- каждая нода знает локальных клиентов и постепенно синхронизирует знания о клиентах всей mesh-сети;
- сообщения доставляются адресно: `клиент -> домашняя нода -> mesh -> домашняя нода получателя -> клиент`.

Ключевой принцип: **клиентская сессия принадлежит клиенту**, а нода только обслуживает эту сессию. Нода не должна самовольно «выкидывать» клиента из-за кратковременной потери HTTP-запроса. Онлайн-статус определяется через WebSocket/heartbeat, а не через сам факт отсутствия нового HTTP-запроса.

---

## 1. Термины

| Термин | Значение |
|---|---|
| `node` / нода | Устройство LilyGo T3, которое поднимает Wi-Fi AP и участвует в LoRa mesh |
| `client` / клиент | Android-приложение пользователя, подключенное к Wi-Fi AP ноды |
| `homeNode` | Нода, к которой клиент подключен сейчас или был подключен последней |
| `nodeId` | Стабильный идентификатор ноды, например `N-812F2B14` |
| `displayName` | Человеческое имя Wi-Fi-точки, например `Lora-1` |
| `username` | Уникальный ник клиента во всей mesh-сети |
| `sessionToken` | Токен текущей клиентской сессии |
| `packetId` | Уникальный ID mesh-пакета для антидублей и ACK |
| `originNodeId` | Нода, где изначально был создан пакет |
| `previousHop` | Нода, от которой текущая нода получила пакет |
| `dstNodeId` | Целевая нода, если пакет адресный |
| `broadcast` | Пакет для распространения по сети через flood/relay |
| `neighbor` | Нода, которую текущая нода слышит напрямую по LoRa |

---

## 2. Общая схема системы

```text
┌──────────────────────────────────────────────────────────────────────┐
│                          LoRa MESH NETWORK                           │
│                                                                      │
│   ┌──────────────┐       LoRa        ┌──────────────┐                │
│   │   Node A     │◄─────────────────►│   Node B     │                │
│   │  Wi-Fi AP    │                   │  Wi-Fi AP    │                │
│   └──────┬───────┘                   └──────┬───────┘                │
│          │                                  │                        │
│   ┌──────┴─────────────┐             ┌──────┴─────────────┐          │
│   │ Android clients    │             │ Android clients    │          │
│   │ Alice, Bob, ...    │             │ Carol, Dave, ...   │          │
│   └────────────────────┘             └────────────────────┘          │
└──────────────────────────────────────────────────────────────────────┘
```

Каждая нода выполняет четыре роли:

1. **Wi-Fi AP** — открытая точка доступа для Android-клиентов.
2. **HTTP API** — регистрация, список клиентов, отправка сообщений, статус.
3. **WebSocket-сервер** — push-доставка входящих сообщений и событий.
4. **LoRa mesh-router** — прием, фильтрация, ретрансляция и подтверждение mesh-пакетов.

---

## 3. Важное архитектурное решение: два имени ноды

Не нужно делать `nodeId` равным `Lora-1`, `Lora-2` и так далее. Номер в имени Wi-Fi может конфликтовать после перезагрузки двух нод, особенно если они стартуют одновременно или временно не видят друг друга.

Поэтому у ноды должны быть два идентификатора:

| Поле | Пример | Назначение |
|---|---|---|
| `nodeId` | `N-812F2B14` | Стабильный технический ID, используется в mesh-протоколе |
| `displayName` / SSID | `Lora-1` | Удобное имя Wi-Fi-точки для пользователя |

### 3.1 Формирование `nodeId`

`nodeId` создается при старте из MAC/eFuse ESP32:

```cpp
uint64_t chipid = ESP.getEfuseMac();
String nodeId = "N-" + String((uint32_t)(chipid & 0xFFFFFFFF), HEX);
nodeId.toUpperCase();
```

Такой ID должен использоваться в каждом mesh-пакете. Именно он решает проблему, когда нода принимает пакет и ошибочно считает его «своим» только из-за совпадения временного имени.

### 3.2 Формирование SSID

SSID может оставаться человекочитаемым:

```text
Lora-1
Lora-2
Lora-3
```

Но SSID — это только отображаемое имя. Его нельзя использовать как главный ID в mesh.

---

## 4. Старт ноды

### 4.1 Последовательность запуска

```text
BOOT
  │
  ├─► Инициализация Serial/debug
  ├─► Создание стабильного nodeId из MAC/eFuse
  ├─► Инициализация LoRa
  ├─► Случайная задержка 500–3000 мс
  ├─► Wi-Fi scan SSID по маске Lora-*
  ├─► Выбор свободного displayName / SSID
  ├─► Поднятие Wi-Fi AP
  ├─► Старт HTTP API
  ├─► Старт WebSocket API
  ├─► Инициализация таблиц: clients, sessions, neighbors, seenPackets
  ├─► Отправка NODE_HELLO
  ├─► Запрос SYNC у соседей
  └─► RUNNING
```

### 4.2 Почему одного Wi-Fi scan недостаточно

Wi-Fi scan видит только AP-часть нод. Он не гарантирует, что все LoRa-ноды рядом будут обнаружены. Поэтому Wi-Fi scan годится только для выбора SSID, но не для построения mesh-карты.

Mesh-топология должна строиться через LoRa-служебные пакеты:

- `NODE_HELLO`;
- `NODE_HEARTBEAT`;
- `NEIGHBOR_TABLE_UPDATE`;
- `NODE_SYNC_REQUEST`;
- `NODE_SYNC_RESPONSE`.

---

## 5. Таблица прямых соседей и ретрансляция

### 5.1 Зона прямой связи

Каждая нода слышит только часть сети. Например:

```text
Node_A  <──── нет прямой связи ────>  Node_C
   ▲                                      ▲
   │                                      │
   └────────────── Node_B ────────────────┘
```

В этой схеме:

- `Node_A` не слышит `Node_C` напрямую;
- `Node_C` не слышит `Node_A` напрямую;
- `Node_B` слышит обе ноды;
- `Node_B` должна ретранслировать сообщения между ними.

### 5.2 Neighbor table

Каждая нода хранит таблицу прямых соседей:

```cpp
struct NeighborRecord {
    char nodeId[12];
    uint32_t lastSeenMs;
    int16_t lastRssi;
    float lastSnr;
    uint8_t missedHeartbeats;
};
```

Пример:

```text
Node_A знает: Node_B
Node_B знает: Node_A, Node_C
Node_C знает: Node_B
```

Таблица обновляется через `NODE_HEARTBEAT`.

Если heartbeat не приходил дольше `NODE_TIMEOUT_MS`, сосед считается недоступным.

### 5.3 Зачем нужна таблица соседей

Она нужна, чтобы нода понимала:

- от кого пришел пакет;
- кому пакет уже мог быть доставлен напрямую;
- кому его еще нужно переслать;
- куда не надо отправлять пакет обратно;
- какой следующий hop лучше использовать для адресной доставки.

---

## 6. Антидубли и собственные пакеты

Это критическая часть mesh-протокола.

Каждый пакет обязан иметь:

```cpp
struct MeshHeader {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  ttl;
    uint8_t  flags;
    uint32_t originNodeHash;
    uint32_t srcNodeHash;
    uint32_t dstNodeHash;
    uint32_t packetSeq;
    uint32_t packetId;
};
```

Где:

| Поле | Значение |
|---|---|
| `originNodeHash` | кто изначально создал пакет |
| `srcNodeHash` | кто отправил текущий radio-hop |
| `dstNodeHash` | целевая нода или broadcast |
| `packetSeq` | счетчик пакетов origin-ноды |
| `packetId` | хеш/ID пакета для антидублей |

### 6.1 Правило обработки собственного пакета

Нода должна дропать пакет как собственный только если:

```text
packet.originNodeHash == myNodeHash
```

Нельзя дропать пакет только потому, что `srcNodeHash == myNodeHash`, если пакет мог вернуться после ретрансляции. Для защиты от повторов используется `seenPackets`.

### 6.2 seenPackets

Каждая нода хранит короткий кэш уже обработанных пакетов:

```cpp
struct SeenPacket {
    uint32_t packetId;
    uint32_t originNodeHash;
    uint32_t seenAtMs;
};
```

Правило:

```text
если packetId уже есть в seenPackets:
    DROP duplicate
иначе:
    добавить packetId в seenPackets
    обработать пакет
```

Срок хранения записи: 2–5 минут.

---

## 7. Логика ретрансляции broadcast-пакетов

Broadcast-пакеты нужны для:

- `NODE_HELLO`;
- `NODE_HEARTBEAT`;
- `CLIENT_UPDATE`;
- части `NODE_SYNC_REQUEST`.

### 7.1 Базовое правило flood-relay

```text
При получении broadcast-пакета:
  1. Проверить magic/version.
  2. Если originNodeHash == myNodeHash → DROP own origin.
  3. Если packetId уже видели → DROP duplicate.
  4. Обработать payload локально.
  5. Если ttl == 0 → не пересылать.
  6. Уменьшить ttl на 1.
  7. Поставить srcNodeHash = myNodeHash.
  8. Переслать пакет дальше.
```

### 7.2 Защита от мгновенных коллизий

Перед ретрансляцией нужно добавить небольшой jitter:

```text
relayDelay = random(50ms ... 250ms)
```

Это снижает вероятность, что несколько нод одновременно переотправят один и тот же пакет.

### 7.3 Кому не надо пересылать

Если используется простая LoRa broadcast-передача, физически нельзя выбрать конкретного соседа. Поэтому защита делается не через «не отправлять Node_C обратно», а через:

- `originNodeHash`;
- `srcNodeHash`;
- `previousHop`;
- `seenPackets`;
- `ttl`.

То есть пакет может быть услышан старой нодой повторно, но будет отброшен как duplicate.

---

## 8. Адресная доставка и маршруты

Для личных сообщений лучше использовать не бесконечный broadcast, а адресный пакет `MSG_FORWARD`.

### 8.1 Route table

Нода хранит простую таблицу маршрутов:

```cpp
struct RouteRecord {
    uint32_t dstNodeHash;
    uint32_t nextHopHash;
    uint8_t hopCount;
    int16_t rssi;
    uint32_t updatedAtMs;
};
```

Маршруты можно строить из:

- `NODE_HELLO`;
- `NODE_HEARTBEAT`;
- `NEIGHBOR_TABLE_UPDATE`;
- пакетов, которые проходят через ноду.

### 8.2 Если маршрут известен

```text
Alice -> Node_A -> Node_B -> Node_C -> Bob
```

`Node_A` знает, что до `Node_C` следующий hop — `Node_B`, и отправляет `MSG_FORWARD` с:

```text
originNode = Node_A
srcNode    = Node_A
dstNode    = Node_C
nextHop    = Node_B
ttl        = 4
```

### 8.3 Если маршрут неизвестен

Если route table не знает путь до `dstNode`, нода может:

1. отправить `ROUTE_REQUEST`;
2. дождаться `ROUTE_REPLY`;
3. временно использовать controlled flood с малым TTL;
4. вернуть ошибку `ROUTE_NOT_FOUND`, если путь не найден.

Для первой версии проще сделать controlled flood с `ttl = 4`, `seenPackets` и ACK.

---

## 9. Client Registry

### 9.1 Структура клиента

Не рекомендуется хранить всё через `String`, потому что на ESP32 это быстро приводит к фрагментации heap. Лучше использовать фиксированные буферы.

```cpp
struct ClientRecord {
    char username[24];
    char mac[18];
    uint32_t homeNodeHash;
    bool isOnline;
    uint32_t lastSeenMs;
    uint32_t sessionHash;
};
```

### 9.2 Уникальность username

Ник должен быть уникален:

- внутри конкретной ноды;
- во всей известной mesh-сети.

При `POST /api/connect` нода проверяет:

1. нет ли такого username среди локальных клиентов;
2. нет ли такого username в глобальном `clientRegistry`;
3. не находится ли username в состоянии `reserved`.

### 9.3 Резервация ника

Чтобы две ноды одновременно не выдали один ник разным клиентам, вводится механизм `USERNAME_CLAIM`.

```text
Client -> Node_A: хочу username Alice
Node_A:
  1. Проверяет локальный registry.
  2. Создает pending claim.
  3. Рассылает USERNAME_CLAIM(Alice).
  4. Ждет короткое окно конфликта 800–1500 мс.
  5. Если никто не возразил → выдает сессию.
  6. Если пришел USERNAME_CONFLICT → возвращает 409.
```

Для офлайн/разделенной сети конфликт полностью исключить нельзя. Поэтому при последующей синхронизации должен работать deterministic conflict resolution.

### 9.4 Разрешение конфликта ников

Если две части сети независимо выдали одинаковый username:

```text
побеждает запись с меньшим claimTimestamp;
если timestamp равен — меньший homeNodeHash;
проигравший клиент получает USERNAME_CONFLICT и должен выбрать другой ник.
```

---

## 10. Session Management

### 10.1 Структура сессии

```cpp
struct SessionRecord {
    char username[24];
    char token[17];        // 8 байт random HEX = 16 символов + \0
    char clientMac[18];
    uint32_t createdAtMs;
    uint32_t lastActivityMs;
    bool wsConnected;
};
```

### 10.2 Важное правило

Сессия не должна удаляться только потому, что клиент давно не отправлял HTTP-запрос.

HTTP — это разовые запросы. Онлайн-статус должен определяться через:

- WebSocket-соединение;
- ping/pong;
- Wi-Fi station list;
- явный `/api/disconnect`.

### 10.3 Таймауты

| Событие | Действие |
|---|---|
| Нет HTTP-активности, но WS жив | клиент онлайн |
| WS отвалился, Wi-Fi station есть | grace-period 30–60 сек |
| WS отвалился, Wi-Fi station нет | клиент offline |
| Нет активности больше `SESSION_IDLE_TIMEOUT` | сессия протухает |
| `/api/disconnect` | сессия закрывается сразу |

Рекомендуемые значения:

```cpp
#define SESSION_IDLE_TIMEOUT_MS  30UL * 60UL * 1000UL
#define CLIENT_OFFLINE_GRACE_MS  45UL * 1000UL
#define WS_PING_INTERVAL_MS      15UL * 1000UL
#define WS_PONG_TIMEOUT_MS       5UL  * 1000UL
```

---

## 11. HTTP API

Базовый адрес ноды в AP-режиме:

```text
http://192.168.4.1/api
```

### 11.1 `POST /api/connect`

Регистрация или восстановление клиентской сессии.

Request:

```json
{
  "username": "Alice",
  "clientId": "android-generated-stable-id",
  "mac": "AA:BB:CC:DD:EE:FF"
}
```

Response `200 OK`:

```json
{
  "status": "ok",
  "username": "Alice",
  "nodeId": "N-812F2B14",
  "displayName": "Lora-1",
  "sessionToken": "A1B2C3D4E5F60708",
  "sessionTtlSec": 1800
}
```

Response `202 Accepted`, если идет проверка ника по mesh:

```json
{
  "status": "pending",
  "code": "USERNAME_CLAIM_IN_PROGRESS",
  "retryAfterMs": 1000
}
```

Response `409 Conflict`:

```json
{
  "status": "error",
  "code": "USERNAME_TAKEN",
  "message": "Этот ник уже занят"
}
```

### 11.2 `GET /api/clients`

Headers:

```text
X-Username: Alice
X-Session-Token: A1B2C3D4E5F60708
```

Response:

```json
{
  "status": "ok",
  "totalClients": 3,
  "clients": [
    {
      "username": "Alice",
      "homeNode": "N-812F2B14",
      "displayNode": "Lora-1",
      "isOnline": true,
      "isLocal": true
    }
  ]
}
```

### 11.3 `POST /api/send`

Request:

```json
{
  "to": "Bob",
  "clientMessageId": "app-local-msg-001",
  "content": "Привет",
  "sessionToken": "A1B2C3D4E5F60708"
}
```

`from` лучше брать из сессии, а не доверять полю из body.

Response `202 Accepted`:

```json
{
  "status": "accepted",
  "messageId": "M-812F2B14-00000042",
  "deliveryStatus": "pending"
}
```

### 11.4 `GET /api/status`

Response:

```json
{
  "nodeId": "N-812F2B14",
  "displayName": "Lora-1",
  "uptimeMs": 3600000,
  "localClients": 2,
  "knownClients": 8,
  "neighbors": 3,
  "freeHeap": 334064,
  "minFreeHeap": 328056,
  "freePsram": 0,
  "loraQueue": 4,
  "seenPackets": 27
}
```

### 11.5 `POST /api/disconnect`

Request:

```json
{
  "sessionToken": "A1B2C3D4E5F60708"
}
```

Response:

```json
{
  "status": "ok"
}
```

---

## 12. WebSocket API

Подключение:

```text
ws://192.168.4.1:81/ws?username=Alice&token=A1B2C3D4E5F60708
```

### 12.1 Типы WS-событий

#### Входящее сообщение

```json
{
  "type": "MESSAGE_INCOMING",
  "messageId": "M-39AF1002-00000011",
  "from": "Bob",
  "content": "Привет, Alice!",
  "timestamp": 1712345700
}
```

#### ACK от клиента

```json
{
  "type": "MESSAGE_ACK",
  "messageId": "M-39AF1002-00000011",
  "status": "received"
}
```

#### Статус доставки

```json
{
  "type": "MESSAGE_STATUS",
  "messageId": "M-812F2B14-00000042",
  "status": "delivered"
}
```

#### Событие сети

```json
{
  "type": "NETWORK_EVENT",
  "event": "USER_JOINED",
  "username": "Dave",
  "homeNode": "N-ABCD1234"
}
```

#### Ping/pong

```json
{ "type": "PING", "t": 1712345700 }
{ "type": "PONG", "t": 1712345700 }
```

---

## 13. LoRa mesh protocol

### 13.1 Важное ограничение payload

У LoRa-пакета есть ограничение по размеру. Если общий packet max около 255 байт, то после заголовка под payload остается меньше. Поэтому JSON в LoRa лучше не использовать для частых пакетов.

Рекомендуется:

- HTTP/WS с Android — JSON;
- LoRa между нодами — компактный бинарный формат или очень короткий JSON только для отладки.

### 13.2 Конфигурация LoRa

Для Европы обычно используется 868 MHz. Для других регионов частота должна соответствовать местным правилам.

```cpp
#define LORA_FREQUENCY      868E6
#define LORA_BW             125E3
#define LORA_SF             9
#define LORA_CR             5
#define LORA_TX_POWER       17
#define LORA_SYNC_WORD      0x12
```

Если используется модуль на 433/470 MHz, нужно менять частоту и антенну под конкретный модуль.

### 13.3 Типы mesh-пакетов

| Код | Тип | Назначение |
|---:|---|---|
| `0x01` | `NODE_HELLO` | Нода появилась в сети |
| `0x02` | `NODE_HEARTBEAT` | Нода жива |
| `0x03` | `NODE_BYE` | Нода выключается |
| `0x04` | `NODE_SYNC_REQUEST` | Запрос синхронизации |
| `0x05` | `NODE_SYNC_RESPONSE` | Ответ синхронизации |
| `0x06` | `NEIGHBOR_TABLE_UPDATE` | Рассылка таблицы соседей |
| `0x10` | `USERNAME_CLAIM` | Резервация ника |
| `0x11` | `USERNAME_CONFLICT` | Конфликт ника |
| `0x12` | `CLIENT_UPDATE` | Изменение состояния клиента |
| `0x20` | `MSG_FORWARD` | Пересылка сообщения |
| `0x21` | `MSG_NODE_ACK` | Нода получила пакет |
| `0x22` | `MSG_DELIVERY_ACK` | Клиент получил сообщение |
| `0x23` | `MSG_DELIVERY_FAIL` | Сообщение не доставлено |
| `0x30` | `ROUTE_REQUEST` | Поиск маршрута |
| `0x31` | `ROUTE_REPLY` | Ответ о маршруте |
| `0x40` | `FRAGMENT` | Фрагмент большого пакета |

### 13.4 Бинарный заголовок

```cpp
struct MeshHeader {
    uint16_t magic;          // 0xCAFE
    uint8_t  version;        // 1
    uint8_t  type;           // MeshPacketType
    uint8_t  ttl;            // уменьшается при relay
    uint8_t  flags;          // broadcast, ackRequired, fragmented
    uint32_t originNodeHash; // кто создал пакет
    uint32_t srcNodeHash;    // кто отправил текущий hop
    uint32_t dstNodeHash;    // 0xFFFFFFFF = broadcast
    uint32_t packetSeq;      // счетчик origin-ноды
    uint32_t packetId;       // hash(origin + seq + type)
};
```

Размер заголовка: 26 байт без выравнивания. Лучше явно сериализовать поля в byte buffer, а не отправлять `struct` напрямую, чтобы не поймать padding/endianness.

### 13.5 Флаги

```cpp
#define FLAG_BROADCAST     0x01
#define FLAG_ACK_REQUIRED  0x02
#define FLAG_FRAGMENTED    0x04
#define FLAG_ENCRYPTED     0x08 // на будущее
```

---

## 14. Доставка личного сообщения

### 14.1 Сценарий

```text
Alice        Node_A          Node_B          Node_C          Bob
  │            │               │               │              │
  │ POST send  │               │               │              │
  ├───────────►│               │               │              │
  │            │ MSG_FORWARD   │               │              │
  │            ├──────────────►│               │              │
  │            │               │ MSG_FORWARD   │              │
  │            │               ├──────────────►│              │
  │            │               │               │ WS incoming  │
  │            │               │               ├─────────────►│
  │            │               │               │ WS ACK       │
  │            │               │               │◄─────────────┤
  │            │               │ MSG_DELIVERY_ACK             │
  │            │◄──────────────┴───────────────┘              │
  │ WS status  │               │               │              │
  │◄───────────┤               │               │              │
```

### 14.2 ACK-уровни

Есть два разных подтверждения:

| ACK | Кто отправляет | Что означает |
|---|---|---|
| `MSG_NODE_ACK` | соседняя нода | пакет принят следующим hop |
| `MSG_DELIVERY_ACK` | homeNode получателя | сообщение доставлено клиенту через WS и клиент прислал ACK |

`MSG_NODE_ACK` не равен доставке пользователю. Он означает только, что mesh-пакет не потерялся на конкретном radio-hop.

---

## 15. Очереди и повторные отправки

### 15.1 PendingMessage

```cpp
enum MessageStatus : uint8_t {
    MSG_PENDING,
    MSG_FORWARDED,
    MSG_DELIVERED,
    MSG_FAILED
};

struct PendingMessage {
    char messageId[24];
    char fromUsername[24];
    char toUsername[24];
    uint32_t dstNodeHash;
    uint32_t createdAtMs;
    uint32_t lastTryMs;
    uint8_t retryCount;
    MessageStatus status;
};
```

Текст сообщения лучше хранить отдельно в ограниченном буфере или сразу отправлять. Большие сообщения нужно резать на части.

### 15.2 Retry policy

```cpp
#define MSG_MAX_RETRY          3
#define MSG_RETRY_DELAY_MS     10000
#define MSG_TTL_MS             300000
#define MAX_PENDING_MESSAGES   50
```

Правило:

```text
если нет ACK после MSG_RETRY_DELAY_MS:
    retryCount++
    если retryCount <= MSG_MAX_RETRY:
        отправить снова
    иначе:
        status = FAILED
        уведомить отправителя через WS
```

---

## 16. Fragmentation

Фрагментация нужна для:

- полной синхронизации реестра;
- длинных сообщений;
- больших списков клиентов.

### 16.1 Заголовок фрагмента

```cpp
struct FragmentHeader {
    uint16_t fragId;
    uint8_t fragIndex;
    uint8_t fragTotal;
    uint16_t totalSize;
};
```

### 16.2 Правила сборки

```text
1. Каждый fragment имеет одинаковый fragId.
2. Нода хранит буфер сборки до FRAG_TIMEOUT_MS.
3. Если пришли все части — собираем payload и обрабатываем.
4. Если timeout — удаляем неполную сборку.
5. Каждый фрагмент тоже проходит через seenPackets.
```

Рекомендуемые лимиты:

```cpp
#define FRAG_TIMEOUT_MS      15000
#define MAX_FRAGMENTS        8
#define MAX_REASSEMBLY_SIZE  1024
```

---

## 17. Синхронизация состояния

### 17.1 При старте новой ноды

```text
New Node
  │
  ├─► NODE_HELLO broadcast
  ├─► NODE_SYNC_REQUEST broadcast
  │
  ├─◄ NODE_SYNC_RESPONSE от соседей
  │
  ├─► merge ClientRegistry
  ├─► merge Neighbor/Route info
  └─► RUNNING
```

### 17.2 Merge client registry

При получении записи клиента:

```text
если username отсутствует:
    добавить
если username есть и homeNode тот же:
    обновить online/lastSeen
если username есть, но homeNode другой:
    применить conflict resolution
```

### 17.3 Offline не равно удалить

Когда клиент отключился, запись не нужно сразу удалять. Лучше ставить:

```text
isOnline = false
lastSeen = now
```

Это нужно, чтобы:

- сохранить ник на время переподключения;
- корректно обработать поздние ACK;
- не плодить конфликты при кратковременном обрыве.

---

## 18. Wi-Fi и Android-клиенты

### 18.1 AP-настройки

```cpp
#define AP_CHANNEL       6
#define AP_MAX_CLIENTS   8
#define AP_HIDDEN        false
#define AP_IP            IPAddress(192,168,4,1)
```

### 18.2 Ограничение по клиентам

На практике ESP32 не стоит нагружать большим числом WebSocket-клиентов. Для стабильности лучше ограничить:

```cpp
#define MAX_LOCAL_CLIENTS  4
#define MAX_WS_CLIENTS     4
```

Если клиентов больше, HTTP может еще жить, но WebSocket и heap начнут страдать.

---

## 19. Память ESP32

На текущей плате не нужно рассчитывать на PSRAM, если `Free PSRAM: 0 bytes`. Поэтому архитектура должна быть рассчитана на обычный heap.

Рекомендации:

- не хранить большие JSON-строки в RAM;
- не использовать `String` в больших таблицах;
- ограничить количество клиентов;
- ограничить pending-сообщения;
- хранить `nodeHash` вместо длинных строк;
- чистить `seenPackets`, `pendingMessages`, `fragments` по timeout;
- делать `/api/status` для контроля `freeHeap` и `minFreeHeap`.

Рекомендуемые лимиты для первой стабильной версии:

```cpp
#define MAX_KNOWN_CLIENTS      32
#define MAX_LOCAL_CLIENTS      4
#define MAX_NEIGHBORS          16
#define MAX_ROUTES             32
#define MAX_SEEN_PACKETS       80
#define MAX_PENDING_MESSAGES   30
#define MAX_MESSAGE_TEXT       160
```

---

## 20. Константы

```cpp
// Mesh
#define MESH_MAGIC             0xCAFE
#define MESH_VERSION           1
#define BROADCAST_NODE_HASH    0xFFFFFFFF
#define DEFAULT_TTL            4
#define SEEN_PACKET_TTL_MS     180000

// Node
#define NODE_NAME_PREFIX       "Lora-"
#define START_RANDOM_MIN_MS    500
#define START_RANDOM_MAX_MS    3000
#define HEARTBEAT_INTERVAL_MS  30000
#define NODE_TIMEOUT_MS        90000

// Wi-Fi
#define AP_CHANNEL             6
#define AP_MAX_CLIENTS         8
#define AP_HIDDEN              false

// HTTP / WS
#define HTTP_PORT              80
#define WS_PORT                81
#define WS_PING_INTERVAL_MS    15000
#define WS_PONG_TIMEOUT_MS     5000

// Sessions
#define SESSION_TOKEN_BYTES    8
#define SESSION_IDLE_TIMEOUT_MS 1800000
#define CLIENT_OFFLINE_GRACE_MS 45000

// Messages
#define MSG_MAX_RETRY          3
#define MSG_RETRY_DELAY_MS     10000
#define MSG_TTL_MS             300000
#define MAX_MESSAGE_TEXT       160

// LoRa
#define LORA_FREQUENCY         868E6
#define LORA_BW                125E3
#define LORA_SF                9
#define LORA_CR                5
#define LORA_TX_POWER          17
```

---

## 21. Главный цикл ноды

```cpp
void loop() {
    handleLoRaRx();
    processLoRaTxQueue();
    processRelayQueue();
    processPendingMessages();
    cleanupSeenPackets();
    cleanupFragments();
    checkClientSessions();
    sendHeartbeatIfNeeded();
}
```

HTTP/WebSocket могут работать асинхронно, но все тяжелые действия лучше складывать в очереди и обрабатывать в основном loop/task, чтобы не блокировать сетевые callbacks.

---

## 22. Приоритеты LoRa-очереди

LoRa half-duplex: нода не может нормально передавать и принимать одновременно. Поэтому все отправки должны идти через очередь.

Приоритеты:

| Приоритет | Типы |
|---:|---|
| 1 | `MSG_DELIVERY_ACK`, `MSG_NODE_ACK`, `USERNAME_CONFLICT` |
| 2 | `MSG_FORWARD` |
| 3 | `CLIENT_UPDATE`, `USERNAME_CLAIM` |
| 4 | `NODE_HELLO`, `NODE_SYNC_*` |
| 5 | `NODE_HEARTBEAT` |

---

## 23. Ошибки и статусы доставки

| Код | Где возникает | Значение |
|---|---|---|
| `USERNAME_TAKEN` | HTTP connect | ник уже занят |
| `USERNAME_CLAIM_IN_PROGRESS` | HTTP connect | идет проверка ника |
| `SESSION_INVALID` | HTTP/WS | неверный токен |
| `USER_NOT_FOUND` | send | получатель неизвестен |
| `USER_OFFLINE` | send/delivery | получатель офлайн |
| `ROUTE_NOT_FOUND` | mesh | нет маршрута до ноды |
| `MESH_TIMEOUT` | mesh | ACK не пришел |
| `PAYLOAD_TOO_LARGE` | HTTP/mesh | сообщение слишком большое |
| `QUEUE_FULL` | node | очередь переполнена |

---

## 24. Что обязательно реализовать в первой версии

MVP должен включать:

1. стабильный `nodeId` из MAC/eFuse;
2. `packetId`, `originNodeHash`, `srcNodeHash`, `ttl`;
3. `seenPackets` против дублей;
4. broadcast relay для `CLIENT_UPDATE`;
5. WebSocket ACK от клиента;
6. pending queue с retry;
7. проверку уникальности username локально и по известному registry;
8. `/api/status` для отладки heap, клиентов, соседей, очередей;
9. запрет на удаление сессии только из-за отсутствия HTTP-активности.

---

## 25. Что можно отложить

Для первой рабочей версии можно не делать сразу:

- полноценный Dijkstra/AODV routing;
- шифрование payload;
- хранение истории сообщений во Flash;
- идеальную защиту от split-brain конфликтов;
- большие вложения/файлы;
- сложную авторизацию клиентов.

Но заголовок пакета нужно сразу сделать нормальным, иначе потом придется переписывать весь mesh.

---

## 26. Основной алгоритм приема LoRa-пакета

```cpp
void onMeshPacket(const uint8_t* data, size_t len) {
    MeshHeader h;
    if (!parseHeader(data, len, &h)) return;

    if (h.magic != MESH_MAGIC) return;
    if (h.version != MESH_VERSION) return;

    if (h.originNodeHash == myNodeHash) {
        // Это пакет, который изначально создала эта нода.
        // Повторно его не обрабатываем.
        return;
    }

    if (seenPacketsContains(h.originNodeHash, h.packetId)) {
        return;
    }

    seenPacketsAdd(h.originNodeHash, h.packetId);
    updateNeighbor(h.srcNodeHash);
    learnRoute(h.originNodeHash, h.srcNodeHash);

    processPayload(h, data + HEADER_SIZE, len - HEADER_SIZE);

    if (shouldRelay(h)) {
        MeshHeader relay = h;
        relay.ttl--;
        relay.srcNodeHash = myNodeHash;
        enqueueRelay(relay, payload, random(50, 250));
    }
}
```

---

## 27. Правило `shouldRelay`

```cpp
bool shouldRelay(const MeshHeader& h) {
    if (h.ttl == 0) return false;
    if (h.originNodeHash == myNodeHash) return false;

    if (h.flags & FLAG_BROADCAST) {
        return true;
    }

    if (h.dstNodeHash == myNodeHash) {
        return false;
    }

    // Адресный пакет не для нас.
    // Если знаем маршрут — пересылаем к nextHop.
    // Если не знаем — controlled flood с ttl.
    return true;
}
```

---

## 28. Итоговая модель

Система должна работать так:

```text
Android-клиенты говорят только с ближайшей нодой по HTTP/WS.
Ноды говорят между собой только через LoRa.
Каждая нода имеет стабильный технический nodeId.
Каждый mesh-пакет имеет origin, src, dst, seq, packetId и ttl.
Дубли режутся через seenPackets.
Broadcast распространяется controlled flood-ом.
Личные сообщения идут через MSG_FORWARD и подтверждаются ACK.
Клиентская сессия не уничтожается без причины.
Ник уникален в пределах известной mesh-сети и резервируется через claim.
```

