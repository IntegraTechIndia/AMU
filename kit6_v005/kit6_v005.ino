#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <DHT.h>
#include <PZEM004Tv30.h>
#include <math.h>
#include "SPI.h"
#include "TFT_22_ILI9225.h"
#include <../fonts/FreeSans9pt7b.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Function Prototype
void displayRow(String label, String value, int y, uint16_t color = COLOR_WHITE);

// URLs
const char* thingsboardURL = "https://149.56.18.149:32782/api/v1/hWp65ojXaJEssFAOtlto/telemetry";
const char* flaskAPI = "http://149.56.18.149:32780/api/v1/data";

// Kit ID for MongoDB
const char* kitID = "kit6";

// TFT Display Pins
#define TFT_RST 26
#define TFT_RS  25
#define TFT_CS  15
#define TFT_SDI 13
#define TFT_CLK 14
#define TFT_LED 0
#define TFT_BRIGHTNESS 200

SPIClass hspi(HSPI);
TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

// Pin Definitions
#define NTC_THERMISTOR_PIN 34
#define THERMISTORNOMINAL 10000
#define TEMPERATURENOMINAL 25
#define NUMSAMPLES 10
#define BCOEFFICIENT 3950
#define SERIESRESISTOR 10000

#define DHTPIN 4
#define DHTTYPE DHT11
#define FLOW_SENSOR 17
#define ADP810_ADDR 0x25

#define HX710B_SCK 22
#define HX710B_OUT 21

const int analogPin = 36;
const int relayPin = 5;
int thresholdValue = 4095;

// Sensor Objects
DHT dht(DHTPIN, DHTTYPE);
PZEM004Tv30 pzem(&Serial2, 18, 19);
long zeroOffset = 0;

volatile int pulseCount = 0;
unsigned long lastFlowTime = 0;
float flowRate = 0;

long differentialPressure = 0;
byte checksum = 0;
long baselinePressure = 0;
const float PRESSURE_THRESHOLD = 1;

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 18, 19);
  Wire.begin(32, 33);

  dht.begin();
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);
  pinMode(FLOW_SENSOR, INPUT_PULLUP);
  pinMode(HX710B_SCK, OUTPUT);
  pinMode(HX710B_OUT, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR), pulseCounter, RISING);

  WiFiManager wifiManager;
  wifiManager.autoConnect("ESP32-Config");
  Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());

  ArduinoOTA.setHostname("Home Automation");
  ArduinoOTA.onStart([]() { Serial.println("Start updating..."); });
  ArduinoOTA.onEnd([]() { Serial.println("\nUpdate complete."); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress * 100) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
  });
  ArduinoOTA.begin();

  Serial.println("Calibrating... Do NOT apply pressure!");
  delay(2000);
  zeroOffset = readHX710B_raw();
  calibrateSensor();

  hspi.begin();
  tft.begin(hspi);
  tft.clear();
  tft.setGFXFont(&FreeSans9pt7b);
}

void loop() {
  ArduinoOTA.handle();

  int analogValue = getStableAnalogRead();
  bool motorStatus = (analogValue < thresholdValue);
  digitalWrite(relayPin, motorStatus ? LOW : HIGH);

  float rawNTCTemp = readNTCTemperature();
  float ntcTemp = compensateNTCTemperature(rawNTCTemp, motorStatus);

  float roomTemp = dht.readTemperature();
  float humidity = dht.readHumidity();

  float voltage = pzem.voltage();
  float current = pzem.current();
  float power = pzem.power();
  float frequency = pzem.frequency();
  float pf = pzem.pf();

  if (millis() - lastFlowTime >= 1000) {
    noInterrupts();
    flowRate = (pulseCount / 7.5);
    pulseCount = 0;
    interrupts();
    lastFlowTime = millis();
  }

  float diffPressure = readADP810Pressure();
  long airPressure = readHX710B();

  // Send to ThingsBoard
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (https.begin(client, thingsboardURL)) {
    https.addHeader("Content-Type", "application/json");
    StaticJsonDocument<256> json;
    json["ntcTemp"] = ntcTemp;
    json["roomTemp"] = roomTemp;
    json["humidity"] = humidity;
    json["voltage"] = voltage;
    json["current"] = current;
    json["power"] = power;
    json["frequency"] = frequency;
    json["pf"] = pf;
    json["flowRate"] = flowRate;
    json["motorStatus"] = motorStatus;
    json["diffPressure"] = diffPressure;
    json["airPressure"] = airPressure;
    String payload;
    serializeJson(json, payload);
    int httpCode = https.POST(payload);
    String response = https.getString();
    Serial.printf("ThingsBoard POST Code: %d\n", httpCode);
    Serial.println("ThingsBoard Response: " + response);
    https.end();
  } else {
    Serial.println("Failed to connect to ThingsBoard");
  }

  // Send to Flask API (with kit_id)
  WiFiClient httpClient;
  HTTPClient http;
  if (http.begin(httpClient, flaskAPI)) {
    http.addHeader("Content-Type", "application/json");
    StaticJsonDocument<300> json;
    json["kit_id"] = kitID;
    json["ntcTemp"] = ntcTemp;
    json["roomTemp"] = roomTemp;
    json["humidity"] = humidity;
    json["voltage"] = voltage;
    json["current"] = current;
    json["power"] = power;
    json["frequency"] = frequency;
    json["pf"] = pf;
    json["flowRate"] = flowRate;
    json["motorStatus"] = motorStatus;
    json["diffPressure"] = diffPressure;
    json["airPressure"] = airPressure;
    String payload;
    serializeJson(json, payload);
    int httpCode = http.POST(payload);
    String response = http.getString();
    Serial.printf("Flask API POST Code: %d\n", httpCode);
    Serial.println("Flask API Response: " + response);
    http.end();
  } else {
    Serial.println("Failed to connect to Flask API");
  }

  Serial.printf("NTC Temp: %.2f C\nRoom Temp: %.2f C\nHumidity: %.2f %%\nVoltage: %.2f V\nCurrent: %.2f A\nPower: %.2f W\nFreq: %.2f Hz\nPF: %.2f\nFlow Rate: %.2f L/min\nWater Level: %s\nDiff Pressure: %.2f Pa\nAir Pressure: %ld Pa\n\n",
                ntcTemp, roomTemp, humidity, voltage, current, power, frequency, pf,
                flowRate, motorStatus ? "LOW (Motor ON)" : "HIGH (Motor OFF)",
                diffPressure, airPressure);

  // TFT Display Update
  tft.fillRectangle(0, 0, 176, 220, COLOR_BLACK);
  int y = 6, spacing = 22;
  displayRow("M.T:", String(ntcTemp, 1) + " C", y += spacing);
  displayRow("R.T:", isnan(roomTemp) ? "---" : String(roomTemp, 1) + " C", y += spacing);
  displayRow("Hum:", isnan(humidity) ? "---" : String(humidity, 0) + " %", y += spacing);
  displayRow("V:", isnan(voltage) ? "---" : String(voltage, 1) + " V", y += spacing);
  displayRow("C:", isnan(current) ? "---" : String(current, 2) + " A", y += spacing);
  displayRow("F.R:", String(flowRate, 1) + " L/m", y += spacing);
  displayRow("Motor:", motorStatus ? "ON" : "OFF", y += spacing, motorStatus ? COLOR_GREEN : COLOR_RED);
  displayRow("D.P:", String(diffPressure, 1) + " Pa", y += spacing);
  displayRow("A.P:", String(airPressure) + " Pa", y += spacing);

  delay(1000);
}

void displayRow(String label, String value, int y, uint16_t color) {
  int labelX = 5, valueX = 105;
  tft.drawGFXText(labelX, y, label, COLOR_CYAN);
  tft.drawGFXText(valueX, y, value, color);
}

float readNTCTemperature() {
  float avg = 0;
  for (int i = 0; i < NUMSAMPLES; i++) {
    avg += analogRead(NTC_THERMISTOR_PIN);
    delay(5);
  }
  avg /= NUMSAMPLES;
  float voltage = avg * (3.3 / 4095.0);
  float resistance = SERIESRESISTOR / ((3.3 / voltage) - 1);
  float temp = resistance / THERMISTORNOMINAL;
  temp = log(temp);
  temp /= BCOEFFICIENT;
  temp += 1.0 / (TEMPERATURENOMINAL + 273.15);
  temp = 1.0 / temp;
  return temp - 273.15;
}

float compensateNTCTemperature(float temp, bool motorStatus) {
  float compensation = motorStatus ? 26.0 : 5.0;
  return temp - compensation;
}

int getStableAnalogRead() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(analogPin);
    delay(5);
  }
  return sum / 10;
}

float readADP810Pressure() {
  Wire.beginTransmission(ADP810_ADDR);
  Wire.write(0x37); Wire.write(0x2D);
  Wire.endTransmission();
  delay(10);
  Wire.requestFrom(ADP810_ADDR, 7);
  if (Wire.available() == 7) {
    differentialPressure = Wire.read() << 8 | Wire.read();
    checksum = Wire.read();
    if (differentialPressure & 0x8000) differentialPressure -= 0x10000;
    float result = (differentialPressure - baselinePressure) / 60.0;
    return abs(result) < PRESSURE_THRESHOLD ? 0.0 : result;
  }
  return 0.0;
}

void calibrateSensor() {
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    Wire.beginTransmission(ADP810_ADDR);
    Wire.write(0x37); Wire.write(0x2D);
    Wire.endTransmission();
    delay(10);
    Wire.requestFrom(ADP810_ADDR, 7);
    if (Wire.available() == 7) {
      long p = Wire.read() << 8 | Wire.read();
      if (p & 0x8000) p -= 0x10000;
      sum += p;
    }
    delay(10);
  }
  baselinePressure = sum / 100;
}

long readHX710B_raw() {
  long count = 0;
  while (digitalRead(HX710B_OUT));
  for (int i = 0; i < 24; i++) {
    digitalWrite(HX710B_SCK, HIGH); delayMicroseconds(1);
    count <<= 1;
    digitalWrite(HX710B_SCK, LOW); delayMicroseconds(1);
    if (digitalRead(HX710B_OUT)) count++;
  }
  digitalWrite(HX710B_SCK, HIGH); delayMicroseconds(1);
  digitalWrite(HX710B_SCK, LOW);
  if (count & 0x800000) count |= ~0xFFFFFF;
  return count / 100;
}

long readHX710B() {
  long raw = readHX710B_raw();
  long adjusted = raw - zeroOffset;
  return abs(adjusted) < 20 ? 0 : adjusted;
}
