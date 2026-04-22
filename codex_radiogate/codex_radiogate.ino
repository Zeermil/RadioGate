// Arduino IDE entry point for the RadioGate firmware.
//
// If using Arduino IDE, click Library/Boards Manager links below:
// http://boardsmanager/esp32#esp32
// http://librarymanager#RadioLib
// http://librarymanager#ArduinoJson
// http://librarymanager#AsyncTCP
// http://librarymanager#ESPAsyncWebServer
//
// Real firmware sources live in ./src and are compiled recursively by
// Arduino IDE. setup() and loop() are implemented in src/main.cpp.
//
// LoRaMesher is vendored directly inside ./src, so Arduino IDE only needs
// RadioLib in addition to the ESP32/networking libraries above.

#include <Arduino.h>
