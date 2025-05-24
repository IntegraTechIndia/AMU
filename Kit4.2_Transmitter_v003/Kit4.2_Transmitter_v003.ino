#include <Wire.h>
#include <INA3221.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <math.h>

// === CAN Bus Libraries ===
#include <SPI.h>
#include <mcp2515.h>  // <-- Added

// === System Configuration ===
#define SERIAL_SPEED 115200
#define SHUNT_RESISTOR_MOHM 100

#define RELAY_CHARGER_PIN 22
#define RELAY_MOTOR_PIN   21

#define VOLTAGE_THRESHOLD_HIGH 12.10
#define VOLTAGE_THRESHOLD_LOW  10.20

#define THERMISTOR_PIN 34
#define SERIES_RESISTOR 10000.0
#define NOMINAL_RESISTANCE 10000.0
#define NOMINAL_TEMPERATURE 25.0
#define B_COEFFICIENT 3950.0
#define VREF 1100.0
#define TEMPERATURE_LIMIT 50.0

#define I2C_SDA 16
#define I2C_SCL 17

// === INA3221 ===
TwoWire customWire = TwoWire(0);
INA3221 ina_0(INA3221_ADDR40_GND);

// === CAN Setup ===
MCP2515 mcp2515(5);  // CS pin for MCP2515 (adjust if needed)
struct can_frame canMsg;

bool relayChargingState = true;
bool motorRunning = true;

// === Thermistor Calibration ===
esp_adc_cal_characteristics_t adc_chars;

void current_measure_init() {
  customWire.begin(I2C_SDA, I2C_SCL, 100000);
  ina_0.begin(&customWire);
  ina_0.reset();
  ina_0.setShuntRes(SHUNT_RESISTOR_MOHM, SHUNT_RESISTOR_MOHM, SHUNT_RESISTOR_MOHM);
}

float readMotorTemperatureCelsius() {
  uint32_t raw_adc = 0;
  for (int i = 0; i < 64; i++) raw_adc += adc1_get_raw(ADC1_CHANNEL_6);
  raw_adc /= 64;

  uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(raw_adc, &adc_chars);
  float voltage = voltage_mv / 1000.0;
  float resistance = SERIES_RESISTOR * voltage / (3.3 - voltage);

  float steinhart = resistance / NOMINAL_RESISTANCE;
  steinhart = log(steinhart);
  steinhart /= B_COEFFICIENT;
  steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15);
  steinhart = 1.0 / steinhart;
  float tempC = steinhart - 273.15;

  Serial.printf("Motor Temp: %.2f °C | Resistance: %.2f kΩ\n", tempC, resistance / 1000.0);
  return tempC;
}

void setup() {
  Serial.begin(SERIAL_SPEED);
  delay(1000);

  pinMode(RELAY_CHARGER_PIN, OUTPUT);
  pinMode(RELAY_MOTOR_PIN, OUTPUT);
  digitalWrite(RELAY_CHARGER_PIN, LOW);
  digitalWrite(RELAY_MOTOR_PIN, LOW);
  relayChargingState = true;
  motorRunning = true;

  current_measure_init();

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, VREF, &adc_chars);

  // === CAN Init ===
  SPI.begin();
  if (mcp2515.reset() != MCP2515::ERROR_OK) {
    Serial.println("MCP2515 init failed");
    while (1);
  }
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();
  Serial.println("MCP2515 initialized");

  Serial.println("System Initialized.");
}

void loop() {
  float voltageA1 = ina_0.getVoltage(INA3221_CH1);
  float currentA1 = ina_0.getCurrent(INA3221_CH1) * 1000;

  float voltageA2 = ina_0.getVoltage(INA3221_CH2);
  float currentA2 = ina_0.getCurrent(INA3221_CH2) * 1000;

  float voltageB1 = ina_0.getVoltage(INA3221_CH3);
  float currentB1 = ina_0.getCurrent(INA3221_CH3) * 1000;

  Serial.printf("Charger (A1): %6.0f mA | %4.2f V\n", currentA1, voltageA1);
  Serial.printf("Battery (A2): %6.0f mA | %4.2f V\n", currentA2, voltageA2);
  Serial.printf("Motor   (B1): %6.0f mA | %4.2f V\n", currentB1, voltageB1);

  if (relayChargingState && voltageA2 >= VOLTAGE_THRESHOLD_HIGH) {
    relayChargingState = false;
    digitalWrite(RELAY_CHARGER_PIN, HIGH);
    Serial.println("🔋 Battery full — Charging OFF");
  } else if (!relayChargingState && voltageA2 <= VOLTAGE_THRESHOLD_LOW) {
    relayChargingState = true;
    digitalWrite(RELAY_CHARGER_PIN, LOW);
    Serial.println("🔋 Battery low — Charging ON");
  }

  float motorTemp = readMotorTemperatureCelsius();

  if (motorTemp >= TEMPERATURE_LIMIT && motorRunning) {
    digitalWrite(RELAY_MOTOR_PIN, HIGH);
    motorRunning = false;
    Serial.println("🔥 Motor temp too high — Motor OFF");
  } else if (motorTemp < (TEMPERATURE_LIMIT - 5) && !motorRunning) {
    digitalWrite(RELAY_MOTOR_PIN, LOW);
    motorRunning = true;
    Serial.println("✅ Motor cooled — Motor ON");
  }

  Serial.printf("Relay (Charger): %s | Relay (Motor): %s\n",
                relayChargingState ? "ON (Charging)" : "OFF",
                motorRunning ? "ON (Running)" : "OFF");

  Serial.println("-----------------------------");

  // === SEND SENSOR DATA OVER CAN ===
  int temp_scaled     = (int)(motorTemp * 10);
  int battV_scaled    = (int)(voltageA2 * 100);
  int battC_scaled    = (int)currentA2;
  int chargV_scaled   = (int)(voltageA1 * 100);
  int chargC_scaled   = (int)currentA1;
  int motorV_scaled   = (int)(voltageB1 * 100);
  int motorC_scaled   = (int)currentB1;
  uint8_t relays      = (relayChargingState ? 1 : 0) | (motorRunning ? 2 : 0);

  // Frame 1: Temp, Batt V, Batt C
  canMsg.can_id  = 0x101;
  canMsg.can_dlc = 8;
  canMsg.data[0] = temp_scaled >> 8;
  canMsg.data[1] = temp_scaled & 0xFF;
  canMsg.data[2] = battV_scaled >> 8;
  canMsg.data[3] = battV_scaled & 0xFF;
  canMsg.data[4] = battC_scaled >> 8;
  canMsg.data[5] = battC_scaled & 0xFF;
  canMsg.data[6] = 0;
  canMsg.data[7] = 0;
  mcp2515.sendMessage(&canMsg);

  // Frame 2: Charger V/C + Motor V
  canMsg.can_id  = 0x102;
  canMsg.data[0] = chargV_scaled >> 8;
  canMsg.data[1] = chargV_scaled & 0xFF;
  canMsg.data[2] = chargC_scaled >> 8;
  canMsg.data[3] = chargC_scaled & 0xFF;
  canMsg.data[4] = motorV_scaled >> 8;
  canMsg.data[5] = motorV_scaled & 0xFF;
  canMsg.data[6] = 0;
  canMsg.data[7] = 0;
  mcp2515.sendMessage(&canMsg);

  // Frame 3: Motor C + Relay States
  canMsg.can_id  = 0x103;
  canMsg.data[0] = motorC_scaled >> 8;
  canMsg.data[1] = motorC_scaled & 0xFF;
  canMsg.data[2] = relays;
  canMsg.data[3] = 0;
  canMsg.data[4] = 0;
  canMsg.data[5] = 0;
  canMsg.data[6] = 0;
  canMsg.data[7] = 0;
  mcp2515.sendMessage(&canMsg);

  delay(2000);
}
