#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <PZEM004Tv30.h>

// ========== VL53L0X ==========
Adafruit_VL53L0X lox;

// ========== PZEM ==========
#define PZEM_RX 16
#define PZEM_TX 17
HardwareSerial PZEMSerial(2);
PZEM004Tv30 pzem(PZEMSerial, PZEM_RX, PZEM_TX);

// ========== RECEIVER URL ==========
const char* serverUrl = "http://192.168.1.2/update";

// ========== NETWORK INFO ==========
String ipStr  = "";
String macStr = "";

void setup() {
  Serial.begin(115200);
  delay(1000);

  // -------- WiFiManager --------
  WiFiManager wm;
  wm.setTimeout(180);
  if (!wm.autoConnect("P&")) {
    Serial.println("WiFi failed, restarting...");
    ESP.restart();
  }

  // -------- OTA --------
  ArduinoOTA.setHostname("Plant and Machinery");
  ArduinoOTA.begin();

  // -------- NETWORK INFO --------
  macStr = WiFi.macAddress();
  ipStr  = WiFi.localIP().toString();

  Serial.println("===== TRANSMITTER NETWORK INFO =====");
  Serial.print("MAC Address : ");
  Serial.println(macStr);
  Serial.print("IP Address  : ");
  Serial.println(ipStr);
  Serial.println("===================================");

  // -------- VL53L0X --------
  Wire.begin(21, 22);
  if (!lox.begin()) {
    Serial.println("VL53L0X init failed");
    while (1) delay(1000);
  }

  // -------- PZEM --------
  PZEMSerial.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);
}

void loop() {
  ArduinoOTA.handle();

  // -------- Distance --------
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  float distance =
    (measure.RangeStatus != 4) ? measure.RangeMilliMeter / 10.0 : -1;

  // -------- PZEM --------
  float voltage = pzem.voltage();
  float current = pzem.current();

  // -------- HTTP POST --------
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String data =
      "distance=" + String(distance, 1) +
      "&voltage=" + String(voltage, 2) +
      "&current=" + String(current, 2);

    int code = http.POST(data);
    http.end();

    Serial.print("TX -> ");
    Serial.print(data);
    Serial.print(" | HTTP: ");
    Serial.println(code);
  } else {
    Serial.println("WiFi disconnected");
  }

  delay(5000);
}
