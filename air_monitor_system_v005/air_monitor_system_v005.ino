#include <SDS011.h>
#include <DHT.h>
#include <math.h>
#include "SPI.h"
#include "TFT_22_ILI9225.h"
#include <../fonts/FreeSans9pt7b.h>

#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// === TFT Setup ===
#define TFT_RST 26
#define TFT_RS  25
#define TFT_CS  15
#define TFT_SDI 13
#define TFT_CLK 14
#define TFT_LED 0
#define TFT_BRIGHTNESS 200

SPIClass hspi(HSPI);
TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

// === Sensor Setup ===
#define MQ7_PIN 34
#define MQ135_PIN 35
#define DHTPIN 32
#define DHTTYPE DHT11

#define RL_MQ7 10000.0
#define RL_MQ135 10000.0
float R0_MQ7 = 17933.0;
float R0_MQ135 = 28408.0;

float calFactor_CO   = 0.0787;
float calFactor_PM25 = 0.79;
float calFactor_PM10 = 1.61;

DHT dht(DHTPIN, DHTTYPE);
SDS011 my_sds;

// === Network and API Setup ===
const char* kitID = "kit5";
const char* thingsboardURL = "https://149.56.18.149:32782/api/v1/d49w033atK6W2mxuwLk4/telemetry";
const char* flaskAPI = "http://149.56.18.149:32780/api/v1/data";

void setup() {
  Serial.begin(115200);

  // === Sensor Setup ===
  dht.begin();
  pinMode(MQ7_PIN, INPUT);
  pinMode(MQ135_PIN, INPUT);
  Serial1.begin(9600, SERIAL_8N1, 17, 16);
  my_sds.begin(&Serial1);

  // === TFT Setup ===
  hspi.begin(TFT_CLK, -1, TFT_SDI, TFT_CS);
  tft.begin(hspi);
  tft.setOrientation(0);
  tft.clear();
  tft.setGFXFont(&FreeSans9pt7b);
  tft.drawGFXText(10, 20, "Air Quality Monitor", COLOR_YELLOW);

  // === WiFi Manager ===
  WiFiManager wm;
  wm.setTimeout(180);
  if (!wm.autoConnect("AirMonitor-AP")) {
    Serial.println("WiFi failed. Restarting...");
    ESP.restart();
  }
  Serial.println("WiFi connected: " + WiFi.localIP().toString());

  // === OTA Setup ===
  ArduinoOTA.setHostname("AirMonitor");
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();

  // === Read Sensors ===
  int rawMQ7 = analogRead(MQ7_PIN);
  float Vout_MQ7 = rawMQ7 * (3.3 / 4095.0);
  float RS_MQ7 = (3.3 - Vout_MQ7) * RL_MQ7 / Vout_MQ7;
  float ratio_MQ7 = RS_MQ7 / R0_MQ7;
  float ppm_CO = pow(10, ((log10(ratio_MQ7) - 0.8) / -0.38));
  float calibrated_CO = ppm_CO * calFactor_CO;

  int rawMQ135 = analogRead(MQ135_PIN);
  float Vout_MQ135 = rawMQ135 * (3.3 / 4095.0);
  float RS_MQ135 = (3.3 - Vout_MQ135) * RL_MQ135 / Vout_MQ135;
  float ratio_MQ135 = RS_MQ135 / R0_MQ135;
  float ppm_air = 116.6020682 * pow(ratio_MQ135, -2.769034857);

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  float pm25 = 0, pm10 = 0;
  if (my_sds.read(&pm25, &pm10) != 0) {
    pm25 = 0;
    pm10 = 0;
  }
  float calibrated_PM25 = pm25 * calFactor_PM25;
  float calibrated_PM10 = pm10 * calFactor_PM10;

  // === Display on TFT ===
  tft.fillRectangle(0, 28, 176, 192, COLOR_BLACK);
  int16_t labelX = 5, valueX = 100, y = 45, spacing = 24;

  tft.drawGFXText(labelX, y, "Temp:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(temp, 1) + " C", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "Humidity:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(hum, 1) + " %", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "CO:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(calibrated_CO, 2) + " ppm", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "Air Q:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(ppm_air, 2) + " ppm", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "PM2.5:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(calibrated_PM25, 1) + " ug", COLOR_WHITE); y += spacing;

  tft.drawGFXText(labelX, y, "PM10:", COLOR_CYAN);
  tft.drawGFXText(valueX, y, String(calibrated_PM10, 1) + " ug", COLOR_WHITE);

  // === Send to ThingsBoard ===
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (https.begin(client, thingsboardURL)) {
    https.addHeader("Content-Type", "application/json");
    StaticJsonDocument<256> doc;
    doc["temperature"] = temp;
    doc["humidity"] = hum;
    doc["co"] = calibrated_CO;
    doc["air_quality"] = ppm_air;
    doc["pm25"] = calibrated_PM25;
    doc["pm10"] = calibrated_PM10;
    String json;
    serializeJson(doc, json);
    int code = https.POST(json);
    Serial.printf("ThingsBoard POST: %d\n", code);
    https.end();
  } else {
    Serial.println("ThingsBoard connection failed.");
  }

  // === Send to Flask API ===
  WiFiClient httpClient;
  HTTPClient http;
  if (http.begin(httpClient, flaskAPI)) {
    http.addHeader("Content-Type", "application/json");
    StaticJsonDocument<256> doc;
    doc["kit_id"] = kitID;
    doc["temperature"] = temp;
    doc["humidity"] = hum;
    doc["co"] = calibrated_CO;
    doc["air_quality"] = ppm_air;
    doc["pm25"] = calibrated_PM25;
    doc["pm10"] = calibrated_PM10;
    String json;
    serializeJson(doc, json);
    int code = http.POST(json);
    Serial.printf("Flask POST: %d\n", code);
    http.end();
  } else {
    Serial.println("Flask API connection failed.");
  }

  delay(5000);  // Update interval
}
