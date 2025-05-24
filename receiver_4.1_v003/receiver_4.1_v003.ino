#include <SPI.h>
#include <mcp2515.h>
#include <TFT_22_ILI9225.h>
#include <../fonts/FreeSans9pt7b.h>

#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// === TFT Pin Definitions ===
#define TFT_RST 26
#define TFT_RS  25
#define TFT_CS  15
#define TFT_SDI 13
#define TFT_CLK 14
#define TFT_LED 0
#define TFT_BRIGHTNESS 200

// === HSPI for TFT ===
SPIClass hspi(HSPI);
TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

// === CAN Setup ===
MCP2515 mcp2515(17);  // CS pin for MCP2515
struct can_frame canMsg;

// === Network Configuration ===
const char* kitID = "kit4";
const char* thingsboardURL = "https://149.56.18.149:32782/api/v1/qFDNYT1l4nObqFtkkVxM/telemetry";
const char* flaskAPI = "http://149.56.18.149:32780/api/v1/data";

void setup() {
  Serial.begin(115200);

  // Initialize HSPI for TFT
  hspi.begin(TFT_CLK, -1, TFT_SDI, TFT_CS);
  tft.begin(hspi);
  tft.setOrientation(0);  // Portrait
  tft.clear();

  // --- Persistent Title ---
  tft.setGFXFont(&FreeSans9pt7b);
  tft.drawGFXText(10, 20, "CAN Env Monitor", COLOR_YELLOW);

  // Initialize MCP2515
  SPI.begin();  // VSPI (default SPI for MCP2515)
  if (mcp2515.reset() == MCP2515::ERROR_OK) {
    Serial.println("MCP2515 Initialized");
  } else {
    Serial.println("Error Initializing MCP2515");
    while (1); // halt
  }

  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  // WiFiManager Portal
  WiFiManager wm;
  wm.setTimeout(180);
  if (!wm.autoConnect("CAN-EnvMonitor")) {
    Serial.println("Failed to connect. Restarting...");
    ESP.restart();
  }
  Serial.println("WiFi connected: " + WiFi.localIP().toString());

  // OTA Setup
  ArduinoOTA.setHostname("CAN-Receiver");
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();

  if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
    if (canMsg.can_id == 0x101) {
      int temp = canMsg.data[0];
      int hum  = canMsg.data[1];

      Serial.printf("Received -> Temp: %d °C, Humidity: %d %%\n", temp, hum);

      // --- TFT Display Update ---
      tft.fillRectangle(0, 28, 176, 192, COLOR_BLACK);
      tft.setGFXFont(&FreeSans9pt7b);

      int16_t labelX = 10;
      int16_t valueX = 100;
      int16_t y = 50;
      int16_t spacing = 26;

      tft.drawGFXText(labelX, y, "Temp:", COLOR_CYAN);
      tft.drawGFXText(valueX, y, String(temp) + " C", COLOR_WHITE);

      y += spacing;
      tft.drawGFXText(labelX, y, "Hum :", COLOR_CYAN);
      tft.drawGFXText(valueX, y, String(hum) + " %", COLOR_WHITE);

      // --- Send to ThingsBoard ---
      WiFiClientSecure client;
      client.setInsecure();  // Insecure for demo; use fingerprint or root CA in prod
      HTTPClient https;
      if (https.begin(client, thingsboardURL)) {
        https.addHeader("Content-Type", "application/json");
        StaticJsonDocument<128> doc;
        doc["temperature"] = temp;
        doc["humidity"] = hum;
        String json;
        serializeJson(doc, json);
        int code = https.POST(json);
        Serial.printf("ThingsBoard POST Code: %d\n", code);
        https.end();
      } else {
        Serial.println("ThingsBoard connection failed.");
      }

      // --- Send to Flask + MongoDB ---
      WiFiClient httpClient;
      HTTPClient http;
      if (http.begin(httpClient, flaskAPI)) {
        http.addHeader("Content-Type", "application/json");
        StaticJsonDocument<200> doc;
        doc["kit_id"] = kitID;
        doc["temperature"] = temp;
        doc["humidity"] = hum;
        String json;
        serializeJson(doc, json);
        int code = http.POST(json);
        Serial.printf("Flask POST Code: %d\n", code);
        http.end();
      } else {
        Serial.println("Flask API connection failed.");
      }
    }
  }

  delay(500);  // Update interval
}
