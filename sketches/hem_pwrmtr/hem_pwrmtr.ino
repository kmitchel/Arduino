#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// One Wire init straight from examples.
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 14
#define TEMPERATURE_PRECISION 12 //Fast conversion.

#define BATT_PIN A0       // Battery voltage monitoring (ADC)

// Battery settings (5S Li-ion)
const float BATT_R1 = 220000.0;
const float BATT_R2 = 10000.0;
const float BATT_DIVIDER_RATIO = (BATT_R1 + BATT_R2) / BATT_R2; // 23.0
const float BATT_V_MAX = 21.0;  // 4.2V * 5
const float BATT_V_MIN = 15.0;  // 3.0V * 5
const float ADC_REF_V = 1.0;    // ESP8266 ADC reference is 1V
const unsigned long BATT_READ_INTERVAL_MS = 60000;      // Read battery every 1 min
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#include <PubSubClient.h>
WiFiClient espClient;

PubSubClient mqtt(espClient);

const char* server = "192.168.1.2";
const char* ssid     = "Mitchell";
const char* password = "easypassword";

//Time variables
unsigned long wNewTime, wOldTime, lastTemp, stateDelay;

boolean firstRun = true;
boolean wPulse = false;
boolean conversionInProgress = false;

float battVoltage = 0;
int battPercent = 0;
unsigned long lastBattRead = 0;

#define W_SENSOR 12

ICACHE_RAM_ATTR void wPulsed() {
  wNewTime = micros();
  wPulse = true;
}

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("pwrmtr");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    digitalWrite(2, !digitalRead(2));
  }
  digitalWrite(2, 1);
}

void mqttConnect() {
  mqtt.setServer(server, 1883);
  if (mqtt.connect(WiFi.hostname().c_str())) {
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(0, OUTPUT);
  digitalWrite(0, 1);
  pinMode(2, OUTPUT);
  digitalWrite(2, 0);

  //Enable external interrupt of digital I/O pin 12
  pinMode(W_SENSOR, INPUT);
  attachInterrupt(W_SENSOR, wPulsed, FALLING);

  sensors.setWaitForConversion(false);

  //Set resolution.
  sensors.setResolution(TEMPERATURE_PRECISION);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {    
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    digitalWrite(2, !digitalRead(2));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  wifiConnect();
  mqttConnect();
  ArduinoOTA.setHostname("pwrmtr");
  ArduinoOTA.begin();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }

  if (!mqtt.connected()) {
    mqttConnect();
  }

  if (wPulse) {
    // Pulse less than 300,000 us (300 ms) is > 12kW. Probably noise.
    if (wNewTime - wOldTime > 300000) {
      //Ignore the first pulse. Need 2 good pulses to calc.
      if (!firstRun) {
        //Compute watts with microsecond precision. 1Wh per pulse.
        // 3600s * 1,000,000us / dt
        float currentW = 3600000000.0 / (float)(wNewTime - wOldTime);
        mqtt.publish("power/W", String(currentW, 2).c_str());
      } else {
        firstRun = false;
      }
      wOldTime = wNewTime;
      //Toggle builtin LED for use as a heartbeat.
      digitalWrite(0, !digitalRead(0));
    }
    wPulse = false;
  }

  if (millis() - lastTemp > 15000) {
    //Send temp date every 30 seconds.
    lastTemp = millis();

    //Rescan the 1Wire bus, hot swap devices without power cycling.
    sensors.begin();

    //Non blocking temp conversion.
    conversionInProgress = true;
    sensors.requestTemperatures();
  }
  
  //Temp conversion in progress, ask if its ready.
  if (conversionInProgress) {
        //if (sensors.isConversionComplete()) {
    if (millis() > lastTemp + 2000) {
      //Send address and value for all devices.
      int sensorCount = sensors.getDeviceCount();

      for (int val = 0; val < sensorCount; val++) {
        uint8_t addr[8];
        sensors.getAddress(addr, val);
        String address = "temp/";
        for (uint8_t i = 0; i < 8; i++)
        {
          // zero pad the address if necessary
          if (addr[i] < 16) address = address + String(0);
            address = address + String(addr[i], HEX);
         }

        float temp = sensors.getTempFByIndex(val);

        if (temp > -196.6 && temp < 185) {
          mqtt.publish(address.c_str(), String(temp).c_str());
        }
      }
      conversionInProgress = false;
    }
  }

  // Battery monitoring
  if (millis() - lastBattRead > BATT_READ_INTERVAL_MS || lastBattRead == 0) {
    lastBattRead = millis();
    
    // Read ADC and average a few samples
    long sum = 0;
    for (int i = 0; i < 10; i++) {
      sum += analogRead(BATT_PIN);
      delay(1);
    }
    float avgAdc = sum / 10.0;
    
    // Calculate voltage
    // 10-bit ADC (0-1023) mapped to 0-1.0V (internal ref)
    float vAdc = (avgAdc / 1024.0) * ADC_REF_V;
    battVoltage = vAdc * BATT_DIVIDER_RATIO;
    
    // Calculate percentage (clamped 0-100)
    float percent = (battVoltage - BATT_V_MIN) / (BATT_V_MAX - BATT_V_MIN) * 100.0;
    battPercent = (int)constrain(percent, 0, 100);
    
    // Publish battery state
    if (battVoltage > 0) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%.2f", battVoltage);
      mqtt.publish("pwrmtr/battery/voltage", buf, true);
      snprintf(buf, sizeof(buf), "%d", battPercent);
      mqtt.publish("pwrmtr/battery/percent", buf, true);
    }
  }
  
  mqtt.loop();
  ArduinoOTA.handle();
}

