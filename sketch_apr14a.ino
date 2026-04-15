#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "mbedtls/aes.h"

// =====================
// Wi-Fi AP
// =====================
const char* ssid     = "LoRa-Hub-1";
const char* password = "12345678";

IPAddress apIP(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// =====================
// LoRa pins
// =====================
#define LORA_SCK   5
#define LORA_MISO  19
#define LORA_MOSI  27
#define LORA_SS    18
#define LORA_RST   23
#define LORA_DIO0  26

// =====================
// UDP
// =====================
WiFiUDP udpRecv;
WiFiUDP udpSend;

#define UDP_RECV_PORT 1234
#define UDP_SEND_PORT 1235

// =====================
// AES-256 key (32 bytes)
// =====================
const char* aes_key_str = "4bYrh44mjX7yZ6AAbZAhsQODr7Sk4BVA";

// =====================
// OLED SSD1306
// =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =====================
// Users
// =====================
struct UserEntry {
  IPAddress ip;
  char name[16];
  unsigned long lastSeen;
};

UserEntry users[10];
int userCount = 0;

const char* getUsername(IPAddress ip) {
  for (int i = 0; i < userCount; i++) {
    if (users[i].ip == ip) {
      users[i].lastSeen = millis();
      return users[i].name;
    }
  }

  if (userCount < 10) {
    users[userCount].ip = ip;
    snprintf(users[userCount].name, sizeof(users[userCount].name), "user-%d", userCount + 1);
    users[userCount].lastSeen = millis();
    userCount++;
    return users[userCount - 1].name;
  }

  return "user-?";
}

// =====================
// Display message buffer
// =====================
#define DISPLAY_LINES 8
String displayLines[DISPLAY_LINES];

void renderDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  for (int i = 0; i < DISPLAY_LINES; i++) {
    if (displayLines[i].length() > 0) {
      display.println(displayLines[i]);
    }
  }

  display.display();
}

void clearDisplayBuffer() {
  for (int i = 0; i < DISPLAY_LINES; i++) {
    displayLines[i] = "";
  }
  renderDisplay();
}

void pushDisplayLine(const String& line) {
  for (int i = 0; i < DISPLAY_LINES - 1; i++) {
    displayLines[i] = displayLines[i + 1];
  }
  displayLines[DISPLAY_LINES - 1] = line;
  renderDisplay();
}

void showMessageOnDisplay(const String& msg) {
  const int charsPerLine = 21; // для шрифта size=1 на 128px
  int len = msg.length();

  for (int i = 0; i < len; i += charsPerLine) {
    String part = msg.substring(i, min(i + charsPerLine, len));
    pushDisplayLine(part);
  }
}

// =====================
// Send to all local clients
// =====================
void sendToAllClients(const char* message) {
  for (int i = 0; i < userCount; i++) {
    udpSend.beginPacket(users[i].ip, UDP_SEND_PORT);
    udpSend.print(message);
    udpSend.endPacket();
    delay(5);
  }
}

// =====================
// Encrypt and send LoRa
// =====================
void encryptAndSendLoRa(const char* plaintext) {
  int len = strlen(plaintext);
  if (len > 200) {
    Serial.println("Message too long for AES+LoRa limits!");
    return;
  }

  int padded_len = len + (16 - (len % 16));
  if (padded_len == len) padded_len += 16; // чтобы был хотя бы 1 полный блок padding

  unsigned char input[256] = {0};
  unsigned char output[256] = {0};

  strcpy((char*)input, plaintext);

  unsigned char iv[16];
  for (int i = 0; i < 16; i++) {
    iv[i] = (unsigned char)random(0, 256);
  }

  unsigned char iv_copy[16];
  memcpy(iv_copy, iv, 16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, (const unsigned char*)aes_key_str, 256);

  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded_len, iv_copy, input, output);
  mbedtls_aes_free(&aes);

  LoRa.beginPacket();
  LoRa.write(iv, 16);
  LoRa.write(output, padded_len);
  LoRa.endPacket();
}

// =====================
// Setup
// =====================
void setup() {
  delay(2000);
  Serial.begin(115200);
  randomSeed(esp_random());

  // OLED
  Wire.begin(); // SDA=21, SCL=22 on ESP32 by default
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed!");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("LoRa Hub started");
    display.println("Waiting for msgs...");
    display.display();
  }

  // Wi-Fi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, subnet);
  WiFi.softAP(ssid, password);
  Serial.println("AP: " + WiFi.softAPIP().toString());

  udpRecv.begin(UDP_RECV_PORT);
  udpSend.begin(4321);
  Serial.println("UDP ready");

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa init failed!");
    if (display.width() > 0) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("LoRa init failed!");
      display.display();
    }
  } else {
    LoRa.setTxPower(20);
    Serial.println("LoRa OK");
    if (display.width() > 0) {
      pushDisplayLine("LoRa OK");
    }
  }
}

// =====================
// Loop
// =====================
void loop() {
  // UDP from phone -> LoRa + local clients
  int packetSize = udpRecv.parsePacket();
  if (packetSize) {
    char buf[180];
    int len = udpRecv.read(buf, sizeof(buf) - 1);
    if (len < 0) len = 0;
    buf[len] = '\0';

    IPAddress senderIP = udpRecv.remoteIP();
    const char* username = getUsername(senderIP);

    char message[220];
    snprintf(message, sizeof(message), "%s: %s", username, buf);

    Serial.println("WiFi RX: " + String(message));

    encryptAndSendLoRa(message);
    sendToAllClients(message);
  }

  // LoRa -> decrypt -> display + local clients
  int loraSize = LoRa.parsePacket();
  if (loraSize >= 32) {
    unsigned char iv[16];
    if (LoRa.readBytes(iv, 16) != 16) {
      return;
    }

    int enc_len = loraSize - 16;
    if (enc_len <= 0 || enc_len > 240) {
      return;
    }

    unsigned char input[256] = {0};
    unsigned char output[256] = {0};

    int readLen = LoRa.readBytes(input, enc_len);
    if (readLen != enc_len) {
      return;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, (const unsigned char*)aes_key_str, 256);

    unsigned char iv_copy[16];
    memcpy(iv_copy, iv, 16);

    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, enc_len, iv_copy, input, output);
    mbedtls_aes_free(&aes);

    String incoming = String((char*)output);
    incoming.trim();

    if (incoming.length() > 0) {
      Serial.println("LoRa RX (Decrypted): " + incoming);

      showMessageOnDisplay("RX: " + incoming);
      sendToAllClients(incoming.c_str());
    }
  }
}