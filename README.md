# Полный гайд по запуску `ZedBoard Telegram Bridge`

## 1. Что это за проект

Этот репозиторий реализует MVP двустороннего текстового моста:

`Android -> Bluetooth SPP -> ZedBoard -> LoRa -> Linux host -> Telegram -> обратно`

Что уже есть в проекте:

- Python-сервер, который принимает зашифрованные фреймы по TCP или serial и пересылает сообщения в Telegram.
- Эмулятор радиоканала для локальной проверки без реального LoRa.
- Консольный симулятор ZedBoard для локальной отладки без платы.
- C++ daemon для PetaLinux/ZedBoard, который читает строки из UART, шифрует их, отправляет по `SX127x` через `spidev` и выводит ответы обратно в UART.

Что важно понимать сразу:

- Локальная проверка без железа поддерживается полностью.
- Серверная Python-часть умеет работать с:
  - `TCP`-эмулятором;
  - `serial`-устройством, которое передает байтовые фреймы формата `RB + payload_len + payload`.
- В этом репозитории нет отдельной прошивки или программы для серверного LoRa-модема на Linux host. Для real-hardware режима на стороне Linux host нужен совместимый внешний радиомодем/контроллер, который отдает и принимает уже готовые framed-bytes через serial.

Если ваша цель сейчас просто убедиться, что логика моста работает, начните с раздела "Быстрый локальный запуск без железа".

---

## 2. Три режима запуска

В проекте есть три практических режима:

### Режим A. Локальная эмуляция без железа

Используются:

- `bridge.emulator`
- `bridge.server`
- `bridge.zed_sim`

Этот режим нужен, чтобы проверить:

- Telegram bot;
- AES-ключ;
- ACK/retry;
- двусторонний обмен;
- команды `/ping` и `/status`.

### Режим B. Linux host + Telegram bridge

Используется Python-сервер:

- `python -m bridge.server --config ...`

Этот режим нужен, чтобы проверить именно серверную сторону:

- Telegram long polling;
- очередь исходящих сообщений;
- прием/отправку framed-bytes через `serial`.

### Режим C. Полный запуск с ZedBoard и LoRa

Используются:

- `zedboard-daemon` на PetaLinux;
- серверный Python bridge на Linux host;
- реальный радиоканал LoRa;
- Android-телефон с Bluetooth SPP terminal.

Этот режим требует реального железа и внешнего совместимого серверного радиомодема.

---

## 3. Что понадобится заранее

## 3.1. Железо

Минимальный набор для полного контура:

- `ZedBoard (Zynq-7000)`
- Bluetooth-модуль `HC-05` или совместимый BT-to-UART модуль
- LoRa-модуль `SX1276/SX1278`
- Linux host: mini-PC, обычный Linux-компьютер или Raspberry Pi
- Серверный радиомодем, подключаемый к Linux host по `USB/serial`, который умеет передавать и принимать framed-bytes этого протокола
- Android-смартфон

## 3.2. Программное окружение

Для локальной эмуляции и Linux host:

- Python `3.11+`
- `pip`
- доступ в интернет для Telegram API

Python-зависимости проекта:

- `cryptography`
- `pyserial`
- `python-telegram-bot`

Для сборки `zedboard-daemon`:

- Linux build host или кросс-среда для PetaLinux
- `cmake`
- C++17 compiler
- OpenSSL headers/dev package
- Linux `spidev` headers

## 3.3. Секреты и идентификаторы

Вам понадобятся:

- `bot_token`
- `chat_id`
- один общий AES-128 ключ длиной ровно 16 байт

В конфиге этот ключ задается как `32` hex-символа, например:

```ini
aes_key = 00112233445566778899aabbccddeeff
```

---

## 4. Важные файлы проекта

Основные файлы, на которые вы будете опираться:

- [README.md](/C:/Users/zerik/Desktop/zboard/README.md)
- [specs/protocol.md](/C:/Users/zerik/Desktop/zboard/specs/protocol.md)
- [config/server.example.ini](/C:/Users/zerik/Desktop/zboard/config/server.example.ini)
- [config/zed-sim.example.ini](/C:/Users/zerik/Desktop/zboard/config/zed-sim.example.ini)
- [config/zedboard.example.ini](/C:/Users/zerik/Desktop/zboard/config/zedboard.example.ini)
- [deploy/telegram-bridge.service](/C:/Users/zerik/Desktop/zboard/deploy/telegram-bridge.service)
- [deploy/zedboard-bridge.service](/C:/Users/zerik/Desktop/zboard/deploy/zedboard-bridge.service)
- [bridge/server.py](/C:/Users/zerik/Desktop/zboard/bridge/server.py)
- [bridge/zed_sim.py](/C:/Users/zerik/Desktop/zboard/bridge/zed_sim.py)
- [zedboard-daemon/README.md](/C:/Users/zerik/Desktop/zboard/zedboard-daemon/README.md)

---

## 5. Wiring и аппаратные связи

Ниже приведен практический wiring-уровень. Проверяйте даташиты именно ваших модулей: распиновка может отличаться.

## 5.1. Android -> HC-05 -> ZedBoard

Цель:

- передавать строки текста с телефона в UART ZedBoard.

Что подключается:

- `HC-05 TX` -> `ZedBoard UART RX`
- `HC-05 RX` -> `ZedBoard UART TX`
- `GND` -> `GND`
- `VCC` -> питание согласно вашему модулю

Что проверить до запуска:

- UART-устройство в Linux действительно соответствует вашему подключению.
- В `config/zedboard.example.ini` параметр `[uart] device` совпадает с реальным устройством.
- Baudrate на `HC-05` совпадает с `[uart] baudrate`.

По умолчанию в примере:

```ini
[uart]
device = /dev/ttyPS1
baudrate = 9600
```

Критерий успеха:

- при ручном подключении Android-приложения типа `Bluetooth Serial Terminal` вы видите ответы `/ping` и `/status`.

Если не работает:

- проверьте RX/TX, они часто перепутаны;
- проверьте уровень питания;
- проверьте baudrate;
- проверьте, что устройство действительно `/dev/ttyPS1`, а не другой UART.

## 5.2. ZedBoard -> SX127x

Цель:

- передавать LoRa-пакеты через `spidev`.

Используемые сигналы:

- `SPI MOSI`
- `SPI MISO`
- `SPI SCK`
- `SPI CS`
- `GND`
- `VCC`
- опционально `RESET GPIO`

Что важно:

- daemon ожидает `spidev`-устройство, например `/dev/spidev0.0`
- reset GPIO указывается числом в конфиге

Пример:

```ini
[lora]
spi_device = /dev/spidev0.0
reset_gpio = 906
```

Что проверить до запуска:

- устройство `spidev` существует;
- Linux на плате разрешает доступ к нему;
- модуль питается корректно;
- частота и тип модуля соответствуют вашей стране и вашему железу.

Критерий успеха:

- daemon стартует без ошибки `SX127x did not respond on SPI`.

## 5.3. Linux host -> серверный радиомодем

Цель:

- дать Python-серверу поток framed-bytes через `serial`.

Важно:

- этот репозиторий не содержит серверную прошивку такого модема;
- на Linux host должен быть внешний совместимый serial-узел, который говорит в формате stream-framing из `specs/protocol.md`.

Что проверить:

- у устройства есть serial-port, например `/dev/ttyUSB0`
- скорость `serial_baudrate` совпадает с реальным устройством
- модем реально принимает и отправляет framed payloads, а не просто сырые LoRa байты "как есть"

---

## 6. Безопасность и обязательные operational-правила

- Используйте одинаковый AES-ключ на обеих сторонах.
- AES-ключ должен быть ровно `16` байт, то есть `32` hex-символа в конфиге.
- Не коммитьте реальный `bot_token` в репозиторий.
- `chat_id` должен соответствовать именно тому Telegram-чату, из которого бот будет принимать ответы.
- Частоту `433/868 MHz` выбирайте по вашему модулю и локальным требованиям.
- В одном MVP-контуре подразумевается один клиент и один чат.

---

## 7. Настройка Telegram-бота

## 7.1. Создайте бота

Цель:

- получить `bot_token`.

Шаги:

1. Откройте Telegram.
2. Найдите `@BotFather`.
3. Выполните `/newbot`.
4. Задайте имя и username.
5. Сохраните выданный `bot_token`.

Критерий успеха:

- у вас есть строка вида `123456789:AA...`.

Если не получилось:

- убедитесь, что username бота уникален;
- убедитесь, что вы копируете token полностью.

## 7.2. Получите `chat_id`

Цель:

- узнать ID чата, в который сервер будет отправлять сообщения и из которого будет принимать ответы.

Самый простой способ:

1. Напишите любое сообщение вашему боту в нужном чате.
2. Откройте в браузере:

```text
https://api.telegram.org/bot<ВАШ_BOT_TOKEN>/getUpdates
```

3. Найдите в ответе поле `chat.id`.

Если используете `curl`:

```bash
curl "https://api.telegram.org/bot<ВАШ_BOT_TOKEN>/getUpdates"
```

Критерий успеха:

- у вас есть числовой `chat_id`.

Если не работает:

- сначала отправьте боту хотя бы одно сообщение;
- проверьте, что token корректный;
- убедитесь, что бот не заблокирован.

Ограничение MVP:

- сервер привязан к одному фиксированному `chat_id`.

---

## 8. Подготовка Python-окружения

Цель:

- установить зависимости и убедиться, что Python-часть запускается.

Команды:

```powershell
python -m pip install -e .
```

Если editable-install вам не нужен, можно так:

```powershell
python -m pip install cryptography pyserial python-telegram-bot
```

Проверка:

```powershell
python -m bridge.server --help
python -m bridge.emulator --help
python -m bridge.zed_sim --help
```

Критерий успеха:

- все три команды выводят help и завершаются без traceback.

Если не работает:

- проверьте версию Python;
- проверьте, что `pip` ставит пакеты в тот же Python, которым вы запускаете проект.

---

## 9. Быстрый локальный запуск без железа

Это лучший стартовый сценарий. Он позволяет убедиться, что мост в целом работает, не поднимая ZedBoard и реальный LoRa.

## 9.1. Подготовьте конфиги

Цель:

- настроить один и тот же AES-ключ и Telegram-параметры.

Откройте [config/server.example.ini](/C:/Users/zerik/Desktop/zboard/config/server.example.ini) и измените:

```ini
[bridge]
client_id = 1
aes_key = 00112233445566778899aabbccddeeff
state_path = state/server-state.json

[radio]
mode = tcp
tcp_host = 127.0.0.1
tcp_port = 7002

[telegram]
bot_token = REPLACE_WITH_BOT_TOKEN
chat_id = 123456789
```

Откройте [config/zed-sim.example.ini](/C:/Users/zerik/Desktop/zboard/config/zed-sim.example.ini) и убедитесь, что:

```ini
[bridge]
client_id = 1
aes_key = 00112233445566778899aabbccddeeff

[radio]
mode = tcp
tcp_host = 127.0.0.1
tcp_port = 7001
```

Что важно:

- `client_id` должен совпадать;
- `aes_key` должен совпадать;
- порты должны остаться `7001` и `7002`, если вы используете стандартную схему с эмулятором.

Критерий успеха:

- конфиги сохранены с одинаковым ключом и корректными Telegram-параметрами.

## 9.2. Запустите радио-эмулятор

Цель:

- создать между симулятором ZedBoard и сервером lossless TCP-радиоканал.

Команда:

```powershell
python -m bridge.emulator --client-port 7001 --server-port 7002 --log-level INFO
```

Критерий успеха:

- в терминале появится сообщение о прослушивании двух портов.

Если не работает:

- проверьте, что порты `7001` и `7002` свободны;
- проверьте, что firewall не мешает локальным TCP-соединениям.

## 9.3. Запустите Telegram bridge

Цель:

- поднять серверную сторону, которая пересылает сообщения из "радио" в Telegram и обратно.

Команда:

```powershell
python -m bridge.server --config config/server.example.ini --log-level INFO
```

Критерий успеха:

- в логах появится строка вида:

```text
Bridge started with state: client_id=1 ...
```

Если не работает:

- проверьте `bot_token`;
- проверьте `chat_id`;
- проверьте, что `bridge.emulator` уже запущен;
- проверьте доступ к интернету.

## 9.4. Запустите ZedBoard simulator

Цель:

- эмулировать сторону ZedBoard и ввод с телефона.

Команда:

```powershell
python -m bridge.zed_sim --config config/zed-sim.example.ini --log-level INFO
```

Критерий успеха:

- увидите приглашение:

```text
Zed simulator ready. Type text, /ping, or /status.
```

## 9.5. Проверьте `/ping` и `/status`

Цель:

- проверить базовую логику CLI-сценария.

В окне `zed_sim` введите:

```text
/ping
```

Ожидаемый результат:

```text
PONG
```

Затем:

```text
/status
```

Ожидаемый результат:

```text
client_id=1 next_outbound=... last_inbound=... pending=...
```

Если не работает:

- проверьте, что `zed_sim` действительно запущен;
- это локальная команда симулятора, она не требует Telegram.

## 9.6. Проверьте uplink: симулятор -> Telegram

Цель:

- отправить сообщение через весь логический тракт.

В окне `zed_sim` введите любой текст, например:

```text
hello from zed sim
```

Что должно произойти:

- сервер получит DATA frame;
- сервер отправит текст в Telegram;
- `zed_sim` получит ACK и покажет:

```text
[ack] msg_id=1
```

Критерий успеха:

- сообщение появилось в нужном Telegram-чате;
- `zed_sim` показал ACK.

Если не работает:

- смотрите логи `bridge.server`;
- проверьте `bot_token`, `chat_id`, `aes_key`;
- проверьте, что `bridge.emulator` работает.

## 9.7. Проверьте downlink: Telegram -> симулятор

Цель:

- получить ответ из Telegram обратно в консоль симулятора.

Напишите ответ в том Telegram-чате, который указан в `chat_id`.

Ожидаемый результат в `zed_sim`:

```text
[telegram] <ваш текст>
```

Дополнительно сервер будет ждать ACK с клиентской стороны.

Критерий успеха:

- текст приходит в окно `zed_sim`;
- после этого у сервера не остается "зависшего" pending-сообщения.

---

## 10. Как искусственно проверить retry и потери

Цель:

- убедиться, что ACK/retry работает.

Запустите эмулятор с потерями:

```powershell
python -m bridge.emulator --client-port 7001 --server-port 7002 --drop-rate 0.3 --log-level INFO
```

Можно добавить повреждение кадров:

```powershell
python -m bridge.emulator --client-port 7001 --server-port 7002 --drop-rate 0.2 --corrupt-rate 0.1 --log-level INFO
```

Ожидаемое поведение:

- в `zed_sim` могут появляться строки:

```text
[retry] msg_id=...
```

- на сервере возможны сообщения:

```text
Retrying outbound frame msg_id=...
```

При полном провале доставки:

```text
[fail] msg_id=...
```

или:

```text
Delivery failed after max retries for msg_id=...
```

Критерий успеха:

- при умеренных потерях сообщения все равно иногда проходят;
- при сильных потерях вы видите предсказуемую деградацию, а не падение процесса.

---

## 11. Подготовка Linux host для реального запуска

Этот раздел нужен, когда локальная эмуляция уже работает.

## 11.1. Клонируйте или скопируйте проект на Linux host

Цель:

- получить рабочую директорию на Linux-машине.

Пример:

```bash
mkdir -p /opt/zboard-bridge
cd /opt/zboard-bridge
```

Скопируйте туда содержимое репозитория.

Критерий успеха:

- на Linux host доступны `bridge/`, `config/`, `deploy/`, `pyproject.toml`.

## 11.2. Установите Python-зависимости

Пример на Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y python3 python3-pip python3-venv
cd /opt/zboard-bridge
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e .
```

Критерий успеха:

- `./.venv/bin/python -m bridge.server --help` отрабатывает без ошибок.

## 11.3. Подготовьте реальный server config

Цель:

- перевести сервер из TCP-эмуляции в `serial`-режим.

Создайте рабочий конфиг, например `/etc/zboard-bridge/server.ini`, на основе [config/server.example.ini](/C:/Users/zerik/Desktop/zboard/config/server.example.ini).

Минимальный пример:

```ini
[bridge]
client_id = 1
aes_key = 00112233445566778899aabbccddeeff
state_path = /var/lib/zboard-bridge/server-state.json

[retry]
ack_timeout_ms = 2000
max_retries = 3
poll_interval_ms = 100

[radio]
mode = serial
serial_port = /dev/ttyUSB0
serial_baudrate = 57600
read_timeout_ms = 200

[lora]
frequency_hz = 868000000
bandwidth = 125000
spreading_factor = 7
coding_rate = 5
tx_power = 14
sync_word = 18

[telegram]
bot_token = REPLACE_WITH_BOT_TOKEN
chat_id = 123456789
```

Что нужно отредактировать:

- `aes_key`
- `serial_port`
- `serial_baudrate`
- `bot_token`
- `chat_id`
- при необходимости LoRa-параметры

Что важно:

- Python-сервер сам не управляет SX127x на host-side;
- он ожидает уже совместимый serial endpoint.

Критерий успеха:

- конфиг сохранен и соответствует реальному serial-устройству.

## 11.4. Ручной запуск server bridge

Команда:

```bash
cd /opt/zboard-bridge
./.venv/bin/python -m bridge.server --config /etc/zboard-bridge/server.ini --log-level INFO
```

Критерий успеха:

- процесс стартует;
- нет ошибок открытия serial-порта;
- нет ошибок Telegram initialization;
- в логах появляется `Bridge started with state: ...`.

Если не работает:

- проверьте права на `/dev/ttyUSB0`;
- проверьте, что это действительно то устройство;
- проверьте `bot_token` и сеть;
- проверьте, что внешний радиомодем действительно использует ожидаемое framing-поведение.

---

## 12. Запуск Python bridge через `systemd` на Linux host

Цель:

- сделать автозапуск сервера после загрузки системы.

В проекте есть шаблон:

- [deploy/telegram-bridge.service](/C:/Users/zerik/Desktop/zboard/deploy/telegram-bridge.service)

## 12.1. Подготовьте файл сервиса

Скопируйте шаблон:

```bash
sudo mkdir -p /etc/systemd/system
sudo cp /opt/zboard-bridge/deploy/telegram-bridge.service /etc/systemd/system/telegram-bridge.service
```

Проверьте, что пути совпадают с вашей установкой:

- `WorkingDirectory=/opt/zboard-bridge`
- `ExecStart=/opt/zboard-bridge/.venv/bin/python -m bridge.server --config /etc/zboard-bridge/server.ini`

Если вы используете шаблон без правок, отредактируйте строку `ExecStart`, чтобы она указывала на Python внутри `.venv`.

## 12.2. Включите и запустите сервис

```bash
sudo systemctl daemon-reload
sudo systemctl enable telegram-bridge.service
sudo systemctl start telegram-bridge.service
sudo systemctl status telegram-bridge.service
```

Посмотреть логи:

```bash
journalctl -u telegram-bridge.service -f
```

Критерий успеха:

- сервис в состоянии `active (running)`;
- в логах нет traceback и ошибок открытия радио.

---

## 13. Сборка `zedboard-daemon`

Собирать этот бинарник нужно на Linux build host или через кросс-toolchain. В текущем Windows-окружении он не собирается "из коробки", потому что использует Linux `spidev`.

## 13.1. Установите build-зависимости

Пример для Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libssl-dev linux-libc-dev
```

Если у вас отдельный SDK/PetaLinux toolchain, используйте его вместо системного компилятора.

Критерий успеха:

- доступны `cmake`, `c++/g++`, OpenSSL headers и Linux SPI headers.

## 13.2. Соберите daemon

Из корня проекта:

```bash
cmake -S zedboard-daemon -B build/zedboard-daemon
cmake --build build/zedboard-daemon
```

Ожидаемый результат:

- бинарник `zedboard-bridge` в build-директории.

Если не работает:

- проверьте OpenSSL dev package;
- проверьте, что заголовки Linux SPI доступны;
- проверьте, что вы собираете под Linux/PetaLinux toolchain.

---

## 14. Подготовка ZedBoard/PetaLinux

## 14.1. Подготовьте конфиг daemon

Возьмите за основу [config/zedboard.example.ini](/C:/Users/zerik/Desktop/zboard/config/zedboard.example.ini).

Пример рабочего конфига:

```ini
[bridge]
client_id = 1
aes_key = 00112233445566778899aabbccddeeff
state_path = /var/lib/zedboard-bridge/state.ini

[retry]
ack_timeout_ms = 2000
max_retries = 3
poll_interval_ms = 100

[uart]
device = /dev/ttyPS1
baudrate = 9600

[lora]
spi_device = /dev/spidev0.0
reset_gpio = 906
frequency_hz = 868000000
bandwidth = 125000
spreading_factor = 7
coding_rate = 5
tx_power = 14
sync_word = 18
poll_interval_ms = 50
```

Что обязательно должно совпадать с сервером:

- `client_id`
- `aes_key`
- LoRa radio parameters

Что должно совпадать с железом ZedBoard:

- `uart.device`
- `uart.baudrate`
- `spi_device`
- `reset_gpio`

Критерий успеха:

- конфиг отражает реальное железо.

## 14.2. Перенесите бинарник и конфиг на плату

Пример:

```bash
scp build/zedboard-daemon/zedboard-bridge root@<zedboard-ip>:/usr/local/bin/zedboard-bridge
scp config/zedboard.example.ini root@<zedboard-ip>:/etc/zboard-bridge/zedboard.ini
```

Если каталога еще нет:

```bash
ssh root@<zedboard-ip> "mkdir -p /etc/zboard-bridge /var/lib/zedboard-bridge && chmod +x /usr/local/bin/zedboard-bridge"
```

Лучше сначала создать каталоги, потом копировать файл конфига.

Пример безопаснее:

```bash
ssh root@<zedboard-ip> "mkdir -p /etc/zboard-bridge /var/lib/zedboard-bridge"
scp build/zedboard-daemon/zedboard-bridge root@<zedboard-ip>:/usr/local/bin/zedboard-bridge
scp config/zedboard.example.ini root@<zedboard-ip>:/etc/zboard-bridge/zedboard.ini
ssh root@<zedboard-ip> "chmod +x /usr/local/bin/zedboard-bridge"
```

Критерий успеха:

- бинарник лежит по пути `/usr/local/bin/zedboard-bridge`;
- конфиг лежит по пути `/etc/zboard-bridge/zedboard.ini`.

## 14.3. Ручной запуск daemon

Команда на ZedBoard:

```bash
/usr/local/bin/zedboard-bridge /etc/zboard-bridge/zedboard.ini
```

Что ожидается:

- в stderr/status-выводе будет:

```text
[status] bridge started ...
```

Если модуль SX127x не отвечает, возможна ошибка вида:

```text
Fatal error: SX127x did not respond on SPI
```

Если UART подключен к Bluetooth terminal, вы также увидите статусные строки там.

Критерий успеха:

- daemon стартует;
- не падает при инициализации UART и SPI;
- отвечает на `/ping` и `/status` через Bluetooth terminal.

## 14.4. Проверка Bluetooth terminal

На Android:

1. Откройте приложение типа `Bluetooth Serial Terminal`.
2. Подключитесь к `HC-05`.
3. Отправьте:

```text
/ping
```

Ожидаемый результат:

```text
[status] PONG
```

Затем:

```text
/status
```

Ожидаемый результат:

```text
[status] client_id=... next_outbound=... last_inbound=... pending=... queue=...
```

Критерий успеха:

- Bluetooth terminal получает ответы от daemon.

---

## 15. Запуск `zedboard-daemon` через `systemd`

В проекте есть шаблон:

- [deploy/zedboard-bridge.service](/C:/Users/zerik/Desktop/zboard/deploy/zedboard-bridge.service)

Этот раздел применим только если в вашей сборке PetaLinux включен `systemd`.

## 15.1. Установите unit-файл

На ZedBoard:

```bash
cp /path/to/repo/deploy/zedboard-bridge.service /etc/systemd/system/zedboard-bridge.service
```

Или скопируйте с build host:

```bash
scp deploy/zedboard-bridge.service root@<zedboard-ip>:/etc/systemd/system/zedboard-bridge.service
```

Проверьте `ExecStart`:

```text
ExecStart=/usr/local/bin/zedboard-bridge /etc/zboard-bridge/zedboard.ini
```

## 15.2. Включите сервис

```bash
systemctl daemon-reload
systemctl enable zedboard-bridge.service
systemctl start zedboard-bridge.service
systemctl status zedboard-bridge.service
```

Логи:

```bash
journalctl -u zedboard-bridge.service -f
```

Критерий успеха:

- сервис `active (running)`;
- нет ошибок инициализации UART/SPI;
- при вводе в Bluetooth terminal сообщения уходят в тракт.

---

## 16. Полная end-to-end проверка с железом

Этот раздел нужен после того, как:

- Linux host bridge работает;
- ZedBoard daemon работает;
- оба конца используют одинаковые радиопараметры и AES-ключ.

## 16.1. Проверьте uplink: Android -> Telegram

Цель:

- убедиться, что текст с телефона уходит в Telegram.

Шаги:

1. Убедитесь, что на Linux host запущен `bridge.server`.
2. Убедитесь, что на ZedBoard запущен `zedboard-bridge`.
3. В Android terminal отправьте:

```text
hello from android
```

Ожидаемый результат:

- на ZedBoard появится статус `ack msg_id=...`;
- сообщение появится в Telegram-чате.

Если не работает:

- проверьте AES-ключ;
- проверьте радиочастоту и LoRa params;
- проверьте, что серверный радиомодем совместим по framing;
- проверьте, что на сервере нет ошибок serial/Telegram.

## 16.2. Проверьте downlink: Telegram -> Android

Цель:

- убедиться, что ответ из Telegram приходит на телефон.

Шаги:

1. Напишите сообщение в тот же Telegram-чат.
2. Следите за Bluetooth terminal на Android.

Ожидаемый результат:

- текст сообщения появится в терминале телефона;
- после этого сервер получит ACK и не будет держать pending frame.

Критерий успеха:

- полный двусторонний обмен работает.

## 16.3. Проверьте неправильный ключ

Цель:

- проверить, что система безопасно отклоняет пакеты.

Шаг:

- задайте заведомо другой `aes_key` на одной из сторон.

Ожидаемое поведение:

- сообщения перестанут проходить;
- процессы не должны падать;
- будут ошибки вида про `authentication failed` или `rx error`.

После теста:

- верните одинаковый ключ на обе стороны.

---

## 17. Что считается успешным запуском

### Локальная эмуляция

Успех:

- `bridge.emulator` запущен;
- `bridge.server` стартует без ошибок;
- `bridge.zed_sim` показывает `PONG` на `/ping`;
- сообщение из `zed_sim` появляется в Telegram;
- ответ из Telegram появляется в `zed_sim`;
- при умеренных потерях видны retry, но процессы не падают.

### Linux host + Telegram bridge

Успех:

- сервер открывает `serial` или `tcp`;
- Telegram bot успешно стартует;
- сервис может жить под `systemd`;
- нет постоянных ошибок framing или authorization.

### Полный контур с ZedBoard

Успех:

- `zedboard-daemon` стартует и видит `SX127x`;
- Android terminal получает ответы `/ping` и `/status`;
- uplink и downlink проходят;
- при потере пакетов срабатывает retry;
- неправильный ключ не приводит к крашу процессов.

---

## 18. Troubleshooting

## 18.1. Проблемы с Python-зависимостями

Симптом:

- `ModuleNotFoundError: No module named 'serial'`
- `ModuleNotFoundError: No module named 'telegram'`

Что делать:

```powershell
python -m pip install -e .
```

или:

```powershell
python -m pip install cryptography pyserial python-telegram-bot
```

Проверьте:

```powershell
python -m bridge.server --help
```

## 18.2. Неправильный `bot_token` или `chat_id`

Симптом:

- сервер не отправляет сообщения в Telegram;
- Telegram polling падает с ошибкой авторизации;
- вы пишете в чат, но ответа обратно нет.

Что делать:

- перепроверьте `bot_token`;
- перепроверьте `chat_id`;
- заново вызовите `getUpdates`;
- убедитесь, что бот уже получил сообщение от нужного чата.

## 18.3. Неправильный AES-ключ

Симптом:

- сообщения не проходят;
- появляются ошибки аутентификации;
- ACK не приходит.

Что делать:

- сравните `aes_key` на обеих сторонах побайтно;
- проверьте, что ключ содержит ровно `32` hex-символа;
- уберите лишние пробелы и переносы.

## 18.4. Проблема с `serial port`

Симптом:

- сервер не может открыть `/dev/ttyUSB0`;
- данные не приходят;
- устройство не существует.

Что делать:

- проверьте имя порта;
- проверьте права доступа;
- проверьте, что USB-устройство действительно создалось;
- проверьте `serial_baudrate`.

## 18.5. Проблема с `SPI/spidev` на ZedBoard

Симптом:

- `SX127x did not respond on SPI`
- daemon завершается при старте

Что делать:

- проверьте `spi_device`;
- проверьте wiring `MOSI/MISO/SCK/CS`;
- проверьте питание модуля;
- проверьте `reset_gpio`;
- проверьте, что в PetaLinux включен `spidev`.

## 18.6. Нет ACK

Симптом:

- зависает `pending` состояние;
- появляются `retry msg_id=...`;
- потом `delivery failed`.

Что делать:

- проверьте, что обе стороны используют одинаковый `client_id`;
- проверьте AES-ключ;
- проверьте радиочастоту и параметры LoRa;
- проверьте, что серверный радиомодем действительно возвращает входящие пакеты приложению, а не теряет их внутри своей прошивки.

## 18.7. Проблемы с `systemd`

Симптом:

- сервис сразу выходит;
- `systemctl status` показывает `failed`.

Что делать:

- смотрите:

```bash
journalctl -u telegram-bridge.service -f
journalctl -u zedboard-bridge.service -f
```

- проверьте `ExecStart`;
- проверьте существование конфигов;
- проверьте рабочие каталоги;
- проверьте права на бинарник и serial/SPI устройства.

## 18.8. Проблемы с Bluetooth terminal

Симптом:

- Android подключается, но ответа нет;
- `/ping` не дает `PONG`.

Что делать:

- проверьте pairing с `HC-05`;
- проверьте RX/TX;
- проверьте baudrate;
- проверьте, что daemon на ZedBoard реально запущен;
- проверьте, что выбран правильный UART device.

---

## 19. Рекомендуемый порядок запуска

Если вы поднимаете систему с нуля, самый безопасный порядок такой:

1. Настроить Telegram bot и получить `chat_id`.
2. Пройти локальную эмуляцию:
   - `bridge.emulator`
   - `bridge.server`
   - `bridge.zed_sim`
3. Проверить обмен в обе стороны.
4. Поднять Linux host в `serial`-режиме.
5. Собрать и запустить `zedboard-daemon`.
6. Проверить `/ping` и `/status` с Android.
7. Проверить реальный uplink/downlink через LoRa.
8. После успешного ручного запуска перевести обе стороны на `systemd`.

---

## 20. Краткая шпаргалка команд

### Локальная эмуляция

```powershell
python -m pip install -e .
python -m bridge.emulator --client-port 7001 --server-port 7002 --log-level INFO
python -m bridge.server --config config/server.example.ini --log-level INFO
python -m bridge.zed_sim --config config/zed-sim.example.ini --log-level INFO
```

### Linux host

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e .
./.venv/bin/python -m bridge.server --config /etc/zboard-bridge/server.ini --log-level INFO
```

### Сборка daemon

```bash
cmake -S zedboard-daemon -B build/zedboard-daemon
cmake --build build/zedboard-daemon
```

### Ручной запуск daemon на ZedBoard

```bash
/usr/local/bin/zedboard-bridge /etc/zboard-bridge/zedboard.ini
```

### `systemd`

```bash
sudo systemctl daemon-reload
sudo systemctl enable telegram-bridge.service
sudo systemctl start telegram-bridge.service
sudo systemctl enable zedboard-bridge.service
sudo systemctl start zedboard-bridge.service
```

---

## 21. Последнее важное замечание

Если у вас пока нет серверного радиомодема с serial framing-совместимостью, это не блокирует разработку логики проекта. В таком случае:

- сначала полностью доведите до рабочего состояния локальную эмуляцию;
- затем поднимите ZedBoard отдельно и проверьте UART + SX127x;
- только потом интегрируйте реальный host-side radio endpoint.

Именно такой порядок обычно экономит больше всего времени.
