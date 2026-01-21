/*
 * hem_wtrsft.ino - Water Softener Monitor
 * 
 * Hardware: ESP8266 with flow meter sensor + DS18B20 temp sensors
 * Purpose: Monitors water flow (GPM) and temperature sensors
 * 
 * MQTT Topics:
 *   Publish: water/GPM, temp/{sensor_address}
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
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 14
#define GPM_SENSOR 12

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiClient espClient;
PubSubClient mqtt(espClient);

const char* server = "raspberrypi";
const char* ssid = "Mitchell";
const char* password = "easypassword";

unsigned long lastTemp;
unsigned int gpmPulse = 0;
unsigned long gpmNewTime, gpmOldTime;
boolean conversionInProgress = false;

ICACHE_RAM_ATTR void gpmPulsed() {
  gpmPulse++;
}

void callback(char* topic, byte* payload, unsigned int length) {
  String payloads;
  for (int i = 0; i < length; i++) {
    payloads += (char)payload[i];
  }
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
  mqtt.setCallback(callback);
  if (mqtt.connect(WiFi.hostname().c_str())) {
    // Connected
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(2, OUTPUT);
  digitalWrite(2, 0);

  pinMode(GPM_SENSOR, INPUT_PULLUP);
  attachInterrupt(GPM_SENSOR, gpmPulsed, FALLING);

  sensors.setWaitForConversion(false);

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
  ArduinoOTA.setHostname("wtrsft");
  ArduinoOTA.begin();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }

  if (!mqtt.connected()) {
    mqttConnect();
  }

  // Monitor water flow - 200 pulses per gallon, report every quart (50 pulses)
  if (gpmPulse >= 50) {
    gpmNewTime = millis();
    // Compute GPM: 15000 = (60000ms/min * 0.25gal/quart)
    float currentGPM = 15000.0 / (gpmNewTime - gpmOldTime);
    mqtt.publish("water/GPM", String(currentGPM).c_str());

    gpmOldTime = gpmNewTime;
    // Don't lose track of pulses that occurred in background
    gpmPulse = gpmPulse - 50;
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
