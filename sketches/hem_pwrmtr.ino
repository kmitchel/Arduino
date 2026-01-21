/*
 * hem_pwrmtr.ino - Power Meter Monitor
 * 
 * Hardware: ESP8266 with optical power meter sensor + DS18B20 temp sensors
 * Purpose: Monitors household power consumption and temperature sensors
 * 
 * MQTT Topics:
 *   Publish: power/W, temp/{sensor_address}
 * 
 * Dependencies:
 *   - ESP8266WiFi
 *   - PubSubClient (MQTT)
 *   - OneWire
 *   - DallasTemperature
 *   - ArduinoOTA
 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

#define ONE_WIRE_BUS 14
#define TEMPERATURE_PRECISION 12
#define W_SENSOR 12

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiClient espClient;
PubSubClient mqtt(espClient);

const char* server = "raspberrypi";
const char* ssid = "Mitchell";
const char* password = "easypassword";

unsigned long wNewTime, wOldTime, lastTemp, stateDelay;

boolean firstRun = true;
boolean wPulse = false;
boolean conversionInProgress = false;

ICACHE_RAM_ATTR void wPulsed() {
  wNewTime = millis();
  wPulse = true;
}

void wifiConnect() {
  WiFi.mode(WIFI_STA);
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
    // Connected
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(0, OUTPUT);
  digitalWrite(0, 1);
  pinMode(2, OUTPUT);
  digitalWrite(2, 0);

  pinMode(W_SENSOR, INPUT);
  attachInterrupt(W_SENSOR, wPulsed, FALLING);

  sensors.setWaitForConversion(false);
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
    // Pulse less than 300ms is >24kW - probably noise, drop it
    if (wNewTime - wOldTime > 300) {
      //Ignore the first pulse - need 2 good pulses to calculate
      if (!firstRun) {
        // Compute watts: 3600000ms/hour * 1Wh per pulse
        unsigned int currentW = 3600000.0 / (wNewTime - wOldTime);
        mqtt.publish("power/W", String(currentW).c_str());
      } else {
        firstRun = false;
      }
      wOldTime = wNewTime;
      // Toggle builtin LED as heartbeat
      digitalWrite(0, !digitalRead(0));
    }
    wPulse = false;
  }

  if (millis() - lastTemp > 15000) {
    lastTemp = millis();

    // Rescan 1-Wire bus for hot-swap support
    sensors.begin();

    // Non-blocking temp conversion
    conversionInProgress = true;
    sensors.requestTemperatures();
  }

  // Check if temp conversion is complete
  if (conversionInProgress) {
    if (millis() > lastTemp + 2000) {
      int sensorCount = sensors.getDeviceCount();

      for (int val = 0; val < sensorCount; val++) {
        uint8_t addr[8];
        sensors.getAddress(addr, val);
        
        String address = "temp/";
        for (uint8_t i = 0; i < 8; i++) {
          if (addr[i] < 16) address += "0";
          address += String(addr[i], HEX);
        }

        float temp = sensors.getTempFByIndex(val);

        if (temp > -196.6 && temp < 185) {
          mqtt.publish(address.c_str(), String(temp).c_str());
        }
      }
      conversionInProgress = false;
    }
  }

  mqtt.loop();
  ArduinoOTA.handle();
}
