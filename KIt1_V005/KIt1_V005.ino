#include "SPI.h"
#include "TFT_22_ILI9225.h"
#include <../fonts/FreeSans9pt7b.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// TFT Pins
#define TFT_RST 26
#define TFT_RS  25
#define TFT_CS  15
#define TFT_SDI 13
#define TFT_CLK 14
#define TFT_LED 0
#define TFT_BRIGHTNESS 200

// Kit ID & URLs
const char* kitID = "kit1";
const char* thingsboardURL = "https://149.56.18.149:32782/api/v1/kiakLUCRoqQ4Usqk9zO7/telemetry"; // Replace <YOUR_TOKEN>
const char* flaskAPI = "http://149.56.18.149:32780/api/v1/data";

// SPI & Display
SPIClass hspi(HSPI);
TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

// PZEM (Serial2: RX=16, TX=17)
PZEM004Tv30 pzem(&Serial2, 16, 17);

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  // Display Setup
  hspi.begin();
  tft.begin(hspi);
  tft.clear();
  tft.setGFXFont(&FreeSans9pt7b);
  tft.drawGFXText(10, 20, "Energy Monitor", COLOR_YELLOW);

  // WiFiManager Portal
  WiFiManager wm;
  wm.setTimeout(180);  // 3 mins
  if (!wm.autoConnect("ESP32-Config")) {
    Serial.println("Failed to connect. Restarting...");
    ESP.restart();
  }
  Serial.println("WiFi connected: " + WiFi.localIP().toString());

  // OTA Setup
  ArduinoOTA.setHostname("EnergyMonitor");
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();

  float voltage = pzem.voltage();
  float current = pzem.current();
  float power = pzem.power();
  float frequency = pzem.frequency();
  float pf = pzem.pf();

  // Serial Debug
  Serial.printf("Voltage: %.2f V | Current: %.2f A | Power: %.2f W | Freq: %.2f Hz | PF: %.2f\n",
                voltage, current, power, frequency, pf);

  // TFT Display Update
  tft.fillRectangle(0, 28, 176, 192, COLOR_BLACK);
  tft.setGFXFont(&FreeSans9pt7b);

  int16_t labelX = 5;
  int16_t valueX = 100;
  int16_t y = 45;
  int16_t spacing = 24;

  tft.drawGFXText(labelX, y, "Voltage:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(voltage) ? "---" : String(voltage, 1) + " V", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "Current:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(current) ? "---" : String(current, 2) + " A", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "Power:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(power) ? "---" : String(power, 1) + " W", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "Freq:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(frequency) ? "---" : String(frequency, 1) + " Hz", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "P.F:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(pf) ? "---" : String(pf, 2), COLOR_WHITE);

  // Send to ThingsBoard
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (https.begin(client, thingsboardURL)) {
    https.addHeader("Content-Type", "application/json");
    StaticJsonDocument<256> doc;
    doc["voltage"] = voltage;
    doc["current"] = current;
    doc["power"] = power;
    doc["frequency"] = frequency;
    doc["pf"] = pf;
    String json;
    serializeJson(doc, json);
    int code = https.POST(json);
    Serial.printf("ThingsBoard POST Code: %d\n", code);
    https.end();
  } else {
    Serial.println("ThingsBoard connection failed.");
  }

  // Send to Flask (MongoDB)
  WiFiClient httpClient;
  HTTPClient http;
  if (http.begin(httpClient, flaskAPI)) {
    http.addHeader("Content-Type", "application/json");
    StaticJsonDocument<300> doc;
    doc["kit_id"] = kitID;
    doc["voltage"] = voltage;
    doc["current"] = current;
    doc["power"] = power;
    doc["frequency"] = frequency;
    doc["pf"] = pf;
    String json;
    serializeJson(doc, json);
    int code = http.POST(json);
    Serial.printf("Flask POST Code: %d\n", code);
    http.end();
  } else {
    Serial.println("Flask API connection failed.");
  }

  delay(500);
}
