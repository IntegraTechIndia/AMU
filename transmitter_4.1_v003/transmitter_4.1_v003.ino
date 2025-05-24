#include <SPI.h>
#include <mcp2515.h>
#include <DHT.h>

#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

// === DHT11 Setup ===
#define DHTPIN 22
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// === CAN Setup ===
MCP2515 mcp2515(17);  // CS pin for MCP2515
struct can_frame canMsg;

void setup() {
  Serial.begin(115200);

  // --- DHT11 Initialization ---
  dht.begin();

  // --- MCP2515 Initialization ---
  SPI.begin();
  if (mcp2515.reset() == MCP2515::ERROR_OK) {
    Serial.println("MCP2515 Initialized");
  } else {
    Serial.println("Error Initializing MCP2515");
    while (1); // Halt on failure
  }

  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  // --- WiFiManager ---
  WiFiManager wm;
  wm.setTimeout(180);  // 3 minutes for user to connect
  if (!wm.autoConnect("CAN-Transmitter")) {
    Serial.println("Failed to connect. Restarting...");
    ESP.restart();
  }
  Serial.println("WiFi connected: " + WiFi.localIP().toString());

  // --- OTA Setup ---
  ArduinoOTA.setHostname("CAN-Transmitter");
  ArduinoOTA.onStart([]() {
    Serial.println("Start OTA Update");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd OTA Update");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  Serial.println("Setup complete. Ready to transmit CAN messages.");
}

void loop() {
  ArduinoOTA.handle();

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  Serial.print("Sending -> Temp: ");
  Serial.print(temperature);
  Serial.print(" °C, Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  // Prepare CAN message
  canMsg.can_id  = 0x101;
  canMsg.can_dlc = 8;
  canMsg.data[0] = (int)temperature;
  canMsg.data[1] = (int)humidity;

  // Clear rest of the bytes
  for (int i = 2; i < 8; i++) canMsg.data[i] = 0;

  // Send CAN message
  mcp2515.sendMessage(&canMsg);

  delay(2000);  // Send every 2 seconds
}
