#include <SPI.h>
#include <mcp2515.h>
#include <DHT.h>

// === DHT11 Setup ===
#define DHTPIN 22
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// === CAN Setup ===
MCP2515 mcp2515(17);  // CS pin for MCP2515
struct can_frame canMsg;

void setup() {
  Serial.begin(115200);

  // Initialize DHT sensor
  dht.begin();

  // Initialize SPI and MCP2515
  SPI.begin();
  if (mcp2515.reset() == MCP2515::ERROR_OK) {
    Serial.println("MCP2515 Initialized");
  } else {
    Serial.println("Error Initializing MCP2515");
    while (1); // Halt on failure
  }

  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  Serial.println("Setup complete. Ready to transmit CAN messages.");
}

void loop() {
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

  // Clear remaining bytes
  for (int i = 2; i < 8; i++) canMsg.data[i] = 0;

  // Send CAN message
  mcp2515.sendMessage(&canMsg);

  delay(2000);  // Send every 2 seconds
}
