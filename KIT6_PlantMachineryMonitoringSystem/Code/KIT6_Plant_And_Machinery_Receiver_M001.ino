#include <SPI.h>
#include <mcp2515.h>

#include "TFT_22_ILI9225.h"
#include <../fonts/FreeSans9pt7b.h>

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

// ================= TFT Pins =================
#define TFT_RST 26
#define TFT_RS  25
#define TFT_CS  15
#define TFT_SDI 13
#define TFT_CLK 14
#define TFT_LED 0
#define TFT_BRIGHTNESS 200

// ================= Network =================
const char* kitID = "kit7";
const char* flaskAPI = "http://192.168.1.130:5010/api/v1/data";
const char* thingsboardURL = "https://192.168.1.238:8080/api/v1/esp4.2/telemetry";

// ================= SPI =================
SPIClass hspi(HSPI);   // TFT
SPIClass vspi(VSPI);   // MCP2515

TFT_22_ILI9225 tft(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

// ================= CAN =================
#define MCP_CS 5
MCP2515 mcp2515(MCP_CS, 8000000, &vspi);   // ✅ FIXED
struct can_frame rxMsg;

// ================= Data =================
float motorTempC = NAN;
float batteryVolt = NAN;
int16_t batteryCurrent = 0;

float chargerVolt = NAN;
int16_t chargerCurrent = 0;
float motorVolt = NAN;
int16_t motorCurrent = 0;

bool relayCharging = false;
bool relayMotor = false;

bool has101 = false;
bool has102 = false;
bool has103 = false;

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // -------- WiFiManager --------
  WiFiManager wm;
  if (!wm.autoConnect("KIT4.2-Receiver")) {
    Serial.println("WiFi failed. Restarting...");
    delay(3000);
    ESP.restart();
  }
  Serial.print("WiFi Connected IP: ");
  Serial.println(WiFi.localIP());

  // -------- OTA --------
  ArduinoOTA.setHostname("EV-System_Receiver");
  ArduinoOTA.begin();

  // -------- SPI Init --------
  hspi.begin(TFT_CLK, -1, TFT_SDI, TFT_CS);
  vspi.begin(18, 19, 23, MCP_CS);   // SCK, MISO, MOSI, CS

  // -------- CAN Init --------
  if (mcp2515.reset() != MCP2515::ERROR_OK) {
    Serial.println("MCP2515 reset failed");
    while (1);
  }

  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ); // Change to MCP_16MHZ if needed
  mcp2515.setNormalMode();
  Serial.println("CAN Bus Initialized");

  // -------- TFT Init --------
  tft.begin(hspi);
  tft.setOrientation(0);
  tft.clear();
  tft.setGFXFont(&FreeSans9pt7b);
  tft.drawGFXText(20, 20, "EV SYSTEM", COLOR_YELLOW);
}

// ================= DISPLAY =================
void displayData() {
  tft.fillRectangle(0, 25, 176, 220, COLOR_BLACK);

  int y = 40;
  int dy = 22;

  tft.drawGFXText(5, y, "Motor T:", COLOR_CYAN);
  tft.drawGFXText(100, y, isnan(motorTempC) ? "---" : String(motorTempC, 1) + " C", COLOR_WHITE); y += dy;

  tft.drawGFXText(5, y, "Batt V:", COLOR_CYAN);
  tft.drawGFXText(100, y, isnan(batteryVolt) ? "---" : String(batteryVolt, 2) + " V", COLOR_WHITE); y += dy;

  tft.drawGFXText(5, y, "Batt I:", COLOR_CYAN);
  tft.drawGFXText(100, y, String(batteryCurrent) + " mA", COLOR_WHITE); y += dy;

  tft.drawGFXText(5, y, "Chg V:", COLOR_CYAN);
  tft.drawGFXText(100, y, isnan(chargerVolt) ? "---" : String(chargerVolt, 2) + " V", COLOR_WHITE); y += dy;

  tft.drawGFXText(5, y, "Chg I:", COLOR_CYAN);
  tft.drawGFXText(100, y, String(chargerCurrent) + " mA", COLOR_WHITE); y += dy;

  tft.drawGFXText(5, y, "Motor V:", COLOR_CYAN);
  tft.drawGFXText(100, y, isnan(motorVolt) ? "---" : String(motorVolt, 2) + " V", COLOR_WHITE); y += dy;

  tft.drawGFXText(5, y, "Motor I:", COLOR_CYAN);
  tft.drawGFXText(100, y, String(motorCurrent) + " mA", COLOR_WHITE); y += dy;

  tft.drawGFXText(5, y, "Relay Chg:", COLOR_CYAN);
  tft.drawGFXText(100, y, relayCharging ? "ON" : "OFF", COLOR_WHITE); y += dy;

  tft.drawGFXText(5, y, "Relay Mtr:", COLOR_CYAN);
  tft.drawGFXText(100, y, relayMotor ? "ON" : "OFF", COLOR_WHITE);
}

// ================= HTTP =================
void sendToFlask() {
  HTTPClient http;
  WiFiClient client;

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
    http.POST(payload);
    http.end();
  }
}

void sendToThingsBoard() {
  WiFiClientSecure client;
  client.setInsecure();

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
    https.POST(payload);
    https.end();
  }
}

// ================= LOOP =================
void loop() {
  ArduinoOTA.handle();

  if (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {

    switch (rxMsg.can_id) {

      case 0x101:
        motorTempC = ((int16_t)(rxMsg.data[0] << 8 | rxMsg.data[1])) / 10.0;
        batteryVolt = ((int16_t)(rxMsg.data[2] << 8 | rxMsg.data[3])) / 100.0;
        batteryCurrent = (int16_t)(rxMsg.data[4] << 8 | rxMsg.data[5]);
        has101 = true;
        break;

      case 0x102:
        chargerVolt = ((int16_t)(rxMsg.data[0] << 8 | rxMsg.data[1])) / 100.0;
        chargerCurrent = (int16_t)(rxMsg.data[2] << 8 | rxMsg.data[3]);
        motorVolt = ((int16_t)(rxMsg.data[4] << 8 | rxMsg.data[5])) / 100.0;
        has102 = true;
        break;

      case 0x103:
        motorCurrent = (int16_t)(rxMsg.data[0] << 8 | rxMsg.data[1]);
        relayCharging = rxMsg.data[2] & 0x01;
        relayMotor = rxMsg.data[2] & 0x02;
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

  delay(100);
}
