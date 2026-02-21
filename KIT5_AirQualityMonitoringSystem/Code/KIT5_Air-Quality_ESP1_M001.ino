#include <WiFi.h> 
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include <Wire.h>
#include <ModbusMaster.h>
#include <SPI.h>
#include "TFT_22_ILI9225.h"
#include <../fonts/FreeSans9pt7b.h>

// ================= TFT =================
#define TFT_RST 26
#define TFT_RS  25
#define TFT_CS  15
#define TFT_SDI 13
#define TFT_CLK 14
#define TFT_LED 0
#define TFT_BRIGHTNESS 200

SPIClass hspi(HSPI);
TFT_22_ILI9225 tft(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

// ================= SDS011 =================
#define SDS_RX 16
#define SDS_TX 17
HardwareSerial SerialSDS(1);
float pm25 = 0, pm10 = 0;

// ================= SHT20 =================
#define RXD2 18
#define TXD2 19
HardwareSerial SerialSHT(2);
ModbusMaster sht20;

// ================= ADP810 =================
#define SDA_ADP 33
#define SCL_ADP 32
#define ADP810_ADDR 0x25
TwoWire WireADP(1);

float adp_pressure_pa = 0;
float adp_zero_offset = 0;
float adp_filtered = 0;

// ================= TRANSMITTER DATA =================
WebServer server(80);
String distStr = "0";
String voltStr = "0";
String currStr = "0";

// ================= NETWORK INFO =================
String ipStr = "";
String macStr = "";

// ================= CLOUD =================
const char* kitID = "kit3";
const char* thingsboardURL =
 "https://192.168.1.238:8080/api/v1/KIT3/telemetry";
const char* flaskAPI =
 "http://192.168.1.130:5010/api/v1/data";

// ================= CRC =================
uint8_t adp_crc8(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

// ================= ADP810 READ =================
bool readADP810() {
  WireADP.beginTransmission(ADP810_ADDR);
  WireADP.write(0x37);
  WireADP.write(0x2D);
  if (WireADP.endTransmission() != 0) return false;

  delay(12);
  WireADP.requestFrom(ADP810_ADDR, (uint8_t)9);
  if (WireADP.available() != 9) return false;

  uint8_t dp_m = WireADP.read();
  uint8_t dp_l = WireADP.read();
  WireADP.read();

  WireADP.read(); WireADP.read(); WireADP.read();

  uint8_t sf_m = WireADP.read();
  uint8_t sf_l = WireADP.read();
  WireADP.read();

  int16_t dp_raw = (dp_m << 8) | dp_l;
  uint16_t scale = (sf_m << 8) | sf_l;

  adp_pressure_pa = dp_raw / (float)scale;
  adp_filtered = adp_filtered * 0.9 + (adp_pressure_pa - adp_zero_offset) * 0.1;
  return true;
}

// ================= SDS011 =================
bool readSDS011() {
  static uint8_t buf[10], idx = 0;
  while (SerialSDS.available()) {
    uint8_t c = SerialSDS.read();
    if (idx == 0 && c != 0xAA) continue;
    buf[idx++] = c;
    if (idx == 10) {
      idx = 0;
      if (buf[9] == 0xAB) {
        pm25 = ((buf[3] << 8) | buf[2]) / 10.0;
        pm10 = ((buf[5] << 8) | buf[4]) / 10.0;
        return true;
      }
    }
  }
  return false;
}

// ================= DISPLAY =================
void updateDisplay(float temp, float hum) {
  tft.clear();
  tft.setGFXFont(&FreeSans9pt7b);

  int y = 18;
  tft.drawGFXText(5, y, "Air-Quality", COLOR_YELLOW); y += 18;

  tft.drawGFXText(5, y, "PM2.5:", COLOR_GREEN);
  tft.drawGFXText(85, y, String(pm25,1), COLOR_WHITE); y += 20;

  tft.drawGFXText(5, y, "PM10:", COLOR_GREEN);
  tft.drawGFXText(85, y, String(pm10,1), COLOR_WHITE); y += 20;

  tft.drawGFXText(5, y, "Temp:", COLOR_CYAN);
  tft.drawGFXText(85, y, String(temp,1), COLOR_WHITE); y += 20;

  tft.drawGFXText(5, y, "Hum:", COLOR_CYAN);
  tft.drawGFXText(85, y, String(hum,1), COLOR_WHITE); y += 20;

  tft.drawGFXText(5, y, "Press:", COLOR_ORANGE);
  tft.drawGFXText(85, y, String(adp_filtered,2), COLOR_WHITE); y += 20;

  tft.drawGFXText(5, y, "Dist:", COLOR_BLUE);
  tft.drawGFXText(85, y, distStr, COLOR_WHITE); y += 20;

  tft.drawGFXText(5, y, "Volt:", COLOR_MAGENTA);
  tft.drawGFXText(85, y, voltStr, COLOR_WHITE); y += 20;

  tft.drawGFXText(5, y, "Curr:", COLOR_MAGENTA);
  tft.drawGFXText(85, y, currStr, COLOR_WHITE);
}

// ================= CLOUD =================
void sendToCloud(float temp, float hum) {

  // ---- ThingsBoard HTTPS ----
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    if (https.begin(client, thingsboardURL)) {
      https.addHeader("Content-Type", "application/json");

      StaticJsonDocument<256> doc;
      doc["temperature"] = temp;
      doc["humidity"]    = hum;
      doc["pressure"]    = adp_filtered;
      doc["pm2_5"]       = pm25;
      doc["pm10"]        = pm10;
      doc["distance"]    = distStr.toFloat();
      doc["voltage"]     = voltStr.toFloat();
      doc["current"]     = currStr.toFloat();

      String json;
      serializeJson(doc, json);

      int code = https.POST(json);
      Serial.print("ThingsBoard POST: ");
      Serial.println(code);

      https.end();
    }
  }

  // ---- Flask HTTP ----
  {
    WiFiClient client;
    HTTPClient http;

    if (http.begin(client, flaskAPI)) {
      http.addHeader("Content-Type", "application/json");

      StaticJsonDocument<256> doc;
      doc["kit_id"]      = kitID;
      doc["temperature"] = temp;
      doc["humidity"]    = hum;
      doc["pressure"]    = adp_filtered;
      doc["pm25"]        = pm25;
      doc["pm10"]        = pm10;
      doc["distance"]    = distStr;
      doc["voltage"]     = voltStr;
      doc["current"]     = currStr;
      doc["ip"]          = ipStr;
      doc["mac"]         = macStr;

      String json;
      serializeJson(doc, json);

      int code = http.POST(json);
      Serial.print("Flask POST: ");
      Serial.println(code);

      http.end();
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  WiFiManager wm;
  wm.autoConnect("Kit3-Receiver");

  ArduinoOTA.begin();

  macStr = WiFi.macAddress();
  ipStr  = WiFi.localIP().toString();

  server.on("/update", HTTP_POST, []() {
    distStr = server.arg("distance");
    voltStr = server.arg("voltage");
    currStr = server.arg("current");
    server.send(200, "text/plain", "OK");
  });
  server.begin();

  SerialSDS.begin(9600, SERIAL_8N1, SDS_RX, SDS_TX);
  SerialSHT.begin(9600, SERIAL_8N1, RXD2, TXD2);
  sht20.begin(1, SerialSHT);

  WireADP.begin(SDA_ADP, SCL_ADP, 100000);

  hspi.begin(TFT_CLK, -1, TFT_SDI, TFT_CS);
  tft.begin(hspi);
}

// ================= LOOP =================
void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  static unsigned long last = 0;
  if (millis() - last > 2000) {
    last = millis();

    readSDS011();
    readADP810();

    if (sht20.readInputRegisters(0x0001, 2) == sht20.ku8MBSuccess) {
      float temp = sht20.getResponseBuffer(0) / 100.0;
      float hum  = sht20.getResponseBuffer(1) / 100.0;

      updateDisplay(temp, hum);
      sendToCloud(temp, hum);
    }
  }
}
