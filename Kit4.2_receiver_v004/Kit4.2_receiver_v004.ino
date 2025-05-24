#include <SPI.h>
#include <mcp2515.h>
#include "TFT_22_ILI9225.h"
#include <../fonts/FreeSans9pt7b.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// ===== TFT Pins and Config =====
#define TFT_RST 26
#define TFT_RS  25
#define TFT_CS  15
#define TFT_SDI 13
#define TFT_CLK 14
#define TFT_LED 0
#define TFT_BRIGHTNESS 200

// ===== Network Config =====
const char* ssid = "Devanathan_5G";
const char* password = "";
const char* kitID = "kit4";

const char* flaskAPI = "http://149.56.18.149:32780/api/v1/data";
const char* thingsboardURL = "https://149.56.18.149:32782/api/v1/qFDNYT1l4nObqFtkkVxM/telemetry";

// ===== TFT SPI and Object =====
SPIClass hspi(HSPI);
TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

// ===== CAN MCP2515 Config =====
MCP2515 mcp2515(5);  // CS pin
struct can_frame rxMsg;

// ===== Sensor Data Variables =====
float motorTempC = NAN;
float batteryVolt = NAN;
int16_t batteryCurrent = 0;

float chargerVolt = NAN;
int16_t chargerCurrent = 0;
float motorVolt = NAN;

int16_t motorCurrent = 0;
bool relayCharging = false;
bool relayMotor = false;

bool has101 = false, has102 = false, has103 = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Init CAN MCP2515
  SPI.begin();  // VSPI for MCP2515
  if (mcp2515.reset() != MCP2515::ERROR_OK) {
    Serial.println("MCP2515 Reset failed!");
    while (1);
  }
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  // Init TFT
  hspi.begin(TFT_CLK, -1, TFT_SDI, -1);
  tft.begin(hspi);
  tft.setOrientation(0);
  tft.clear();
  tft.setGFXFont(&FreeSans9pt7b);
  tft.drawGFXText(10, 20, "CAN Sensor Monitor", COLOR_YELLOW);
}

void displayData() {
  tft.fillRectangle(0, 28, 176, 220, COLOR_BLACK);

  int16_t labelX = 5;
  int16_t valueX = 100;
  int16_t y = 40;
  int16_t lineSpacing = 22;

  tft.drawGFXText(labelX, y, "Motor T:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(motorTempC) ? "---" : String(motorTempC, 1) + " C", COLOR_WHITE); y += lineSpacing;

  tft.drawGFXText(labelX, y, "Batt V:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(batteryVolt) ? "---" : String(batteryVolt, 2) + " V", COLOR_WHITE); y += lineSpacing;

  tft.drawGFXText(labelX, y, "Batt I:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(batteryCurrent) + " mA", COLOR_WHITE); y += lineSpacing;

  tft.drawGFXText(labelX, y, "Chg Volt:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(chargerVolt) ? "---" : String(chargerVolt, 2) + " V", COLOR_WHITE); y += lineSpacing;

  tft.drawGFXText(labelX, y, "Chg Curr:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(chargerCurrent) + " mA", COLOR_WHITE); y += lineSpacing;

  tft.drawGFXText(labelX, y, "Motor V:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, isnan(motorVolt) ? "---" : String(motorVolt, 2) + " V", COLOR_WHITE); y += lineSpacing;

  tft.drawGFXText(labelX, y, "Motor I:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(motorCurrent) + " mA", COLOR_WHITE); y += lineSpacing;

  tft.drawGFXText(labelX, y, "Relay Chg:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, relayCharging ? "ON" : "OFF", COLOR_WHITE); y += lineSpacing;

  tft.drawGFXText(labelX, y, "Relay Mtr:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, relayMotor ? "ON" : "OFF", COLOR_WHITE);
}

void sendToFlask() {
  WiFiClient client;
  HTTPClient http;

  if (http.begin(client, flaskAPI)) {
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<512> doc;
    doc["kit_id"] = kitID;
    doc["motor_temp"] = motorTempC;
    doc["battery_voltage"] = batteryVolt;
    doc["battery_current"] = batteryCurrent;
    doc["charger_voltage"] = chargerVolt;
    doc["charger_current"] = chargerCurrent;
    doc["motor_voltage"] = motorVolt;
    doc["motor_current"] = motorCurrent;
    doc["relay_charging"] = relayCharging;
    doc["relay_motor"] = relayMotor;

    String payload;
    serializeJson(doc, payload);
    int code = http.POST(payload);
    Serial.printf("Flask POST Code: %d\n", code);
    http.end();
  } else {
    Serial.println("Flask API connection failed.");
  }
}

void sendToThingsBoard() {
  WiFiClientSecure client;
  client.setInsecure();  // Insecure mode for test; use certificate in production

  HTTPClient https;
  if (https.begin(client, thingsboardURL)) {
    https.addHeader("Content-Type", "application/json");

    StaticJsonDocument<512> doc;
    doc["motor_temp"] = motorTempC;
    doc["battery_voltage"] = batteryVolt;
    doc["battery_current"] = batteryCurrent;
    doc["charger_voltage"] = chargerVolt;
    doc["charger_current"] = chargerCurrent;
    doc["motor_voltage"] = motorVolt;
    doc["motor_current"] = motorCurrent;
    doc["relay_charging"] = relayCharging;
    doc["relay_motor"] = relayMotor;

    String payload;
    serializeJson(doc, payload);
    int code = https.POST(payload);
    Serial.printf("ThingsBoard POST Code: %d\n", code);
    https.end();
  } else {
    Serial.println("ThingsBoard connection failed.");
  }
}

void loop() {
  if (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
    switch (rxMsg.can_id) {
      case 0x101:
        motorTempC     = ((int16_t)(rxMsg.data[0] << 8 | rxMsg.data[1])) / 10.0;
        batteryVolt    = ((int16_t)(rxMsg.data[2] << 8 | rxMsg.data[3])) / 100.0;
        batteryCurrent = (int16_t)(rxMsg.data[4] << 8 | rxMsg.data[5]);

        has101 = true;
        break;

      case 0x102:
        chargerVolt    = ((int16_t)(rxMsg.data[0] << 8 | rxMsg.data[1])) / 100.0;
        chargerCurrent = (int16_t)(rxMsg.data[2] << 8 | rxMsg.data[3]);
        motorVolt      = ((int16_t)(rxMsg.data[4] << 8 | rxMsg.data[5])) / 100.0;

        has102 = true;
        break;

      case 0x103:
        motorCurrent   = (int16_t)(rxMsg.data[0] << 8 | rxMsg.data[1]);
        uint8_t relayBits = rxMsg.data[2];
        relayCharging = relayBits & 0x01;
        relayMotor    = relayBits & 0x02;

        has103 = true;
        break;
    }

    if (has101 && has102 && has103) {
      displayData();
      sendToFlask();
      sendToThingsBoard();
      has101 = has102 = has103 = false;
    }
  }

  delay(100);  // Small delay to avoid busy loop
}
