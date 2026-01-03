#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include "Zigbee.h"

#define TIME_TO_SLEEP  900 
#define DC_ELECTRICAL_MEASUREMENT_ENDPOINT_NUMBER 1
#define uS_TO_S_FACTOR 1000000ULL
#define LED_PIN LED_BUILTIN

enum State {
  Start,
  Read,
  Report,
  Sleep
};

State _currentState = Start;
ZigbeeElectricalMeasurement zbElectricalMeasurement = ZigbeeElectricalMeasurement(DC_ELECTRICAL_MEASUREMENT_ENDPOINT_NUMBER);
int loopCount = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");
  print_wakeup_reason();

  // Initialize hardware
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  switchToExternalAntenna();

  // Init LED and turn it OFF
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Enable Analog input
  pinMode(A0, INPUT);

  initializeZigbeeEnpoint();
  startZigbee();

  changeState(Start);
}

void loop() {
  switch (_currentState) {
    case Start:
      delay(5000);
      digitalWrite(LED_BUILTIN, LOW);
      changeState(Read);
      break;

    case Read:
      readSensors();
      changeState(Report);
      break;

    case Report:
      //report();
      changeState(Sleep);
      break;

    case Sleep:      
      delay(5000);
      digitalWrite(LED_BUILTIN, HIGH);    
      if (loopCount++ == 4)  {
        esp_deep_sleep_start();
      }
      
      changeState(Start);
  }
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void switchToExternalAntenna() {
  // RF switch power on
  pinMode(WIFI_ENABLE, OUTPUT);   
  digitalWrite(WIFI_ENABLE, LOW);
  delay(100);

  // select external antenna
  pinMode(WIFI_ANT_CONFIG, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG, HIGH);
}

void initializeZigbeeEnpoint() {
  Serial.println("Initializing Zigbee...");

  // Optional: set Zigbee device name and model
  zbElectricalMeasurement.setManufacturerAndModel("PanteraLeo", "SnailStopper");

  // Add analog clusters to Zigbee Analog according your needs
  zbElectricalMeasurement.addDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE);
  zbElectricalMeasurement.addDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_CURRENT);
  zbElectricalMeasurement.addDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_POWER);

  // Optional: set Min/max values for the measurements
  zbElectricalMeasurement.setDCMinMaxValue(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 0, 10000);  // 0-10.000V
  zbElectricalMeasurement.setDCMinMaxValue(ZIGBEE_DC_MEASUREMENT_TYPE_CURRENT, 0, 1000);  // 0-1.000A
  zbElectricalMeasurement.setDCMinMaxValue(ZIGBEE_DC_MEASUREMENT_TYPE_POWER, 0, 5000);    // 0-5.000W

  // // Optional: set Multiplier/Divisor for the measurements
  zbElectricalMeasurement.setDCMultiplierDivisor(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, 1, 1000);  // 1/1000 = 0.001V (1 unit of measurement = 0.001V = 1mV)
  zbElectricalMeasurement.setDCMultiplierDivisor(ZIGBEE_DC_MEASUREMENT_TYPE_CURRENT, 1, 1000);  // 1/1000 = 0.001A (1 unit of measurement = 0.001A = 1mA)
  zbElectricalMeasurement.setDCMultiplierDivisor(ZIGBEE_DC_MEASUREMENT_TYPE_POWER, 1, 1000);    // 1/1000 = 0.001W (1 unit of measurement = 0.001W = 1mW)

  // Add endpoints to Zigbee Core
  Zigbee.addEndpoint(&zbElectricalMeasurement);
}

void startZigbee() {
  Serial.println("Starting Zigbee...");
  // When all EPs are registered, start Zigbee in End Device mode
  if (!Zigbee.begin()) {
    Serial.println("Zigbee failed to start!");
    Serial.println("Rebooting...");
    ESP.restart();
  } else {
    Serial.println("Zigbee started successfully!");
  }
  Serial.println("Connecting to network");
  while (!Zigbee.connected()) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("Connected");
}

void changeState(State newState) {
  _currentState = newState;
  Serial.printf("Changed state to %i", newState);
  Serial.println();
}

void readSensors() {
  float vRead = readVoltage();
  float vBatt = convertVoltage(vRead);
  Serial.printf("Updating DC voltage to %f mV\r\n", vBatt);

  zbElectricalMeasurement.setDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE, vBatt);
  zbElectricalMeasurement.setDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_CURRENT, 0);
  zbElectricalMeasurement.setDCMeasurement(ZIGBEE_DC_MEASUREMENT_TYPE_POWER, 0);

  delay(100);
}

float readVoltage() {
  uint32_t vRead = 0;
  for (int i = 0; i < 16; i++) {
    vRead += analogReadMilliVolts(A0);
  }

  float vReadAvg = (float)vRead / 16;
  Serial.printf("Read DC voltage %f mV\r\n", vReadAvg);
  Serial.println(vReadAvg, 3);
  return vReadAvg;
}

float convertVoltage(float measured) {
  const float r1 = 1000; // KΩ
  const float r2 = 100; // KΩ

  return measured * (r1 + r2) / r2;
}

void report() {
  zbElectricalMeasurement.reportDC(ZIGBEE_DC_MEASUREMENT_TYPE_VOLTAGE);
  zbElectricalMeasurement.reportDC(ZIGBEE_DC_MEASUREMENT_TYPE_CURRENT);
  zbElectricalMeasurement.reportDC(ZIGBEE_DC_MEASUREMENT_TYPE_POWER);
}
