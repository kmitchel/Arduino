
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <PubSubClient.h>

// One Wire init straight from examples.
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 14
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiClient espClient;

PubSubClient mqtt(espClient);

#include <Wire.h>

const char* server = "raspberrypi";
const char* ssid     = "Mitchell";
const char* password = "easypassword";

//Time variables
unsigned long lastTemp;

boolean conversionInProgress = false;

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
  }
}

#define GPM_SENSOR 12

unsigned int gpmPulse = 0;
unsigned long gpmNewTime, gpmOldTime;

ICACHE_RAM_ATTR void gpmPulsed() {
  gpmPulse++;
}

void setup() {
  Serial.begin(9600);
  
  pinMode(2, OUTPUT);
  digitalWrite(2, 0);

  //Enable external interrupt of digital I/O pin 11
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

    if (gpmPulse >= 50) {
    gpmNewTime = millis();
    //Compute GPM. 200 pulses per gallon. Send GPM every quart.
    float currentGPM = 15000.0 / (gpmNewTime - gpmOldTime);
    mqtt.publish("water/GPM", String(currentGPM).c_str());

    gpmOldTime = gpmNewTime;
    //Don't loose track of any new pulses that may have occured in background.
    gpmPulse = gpmPulse - 50;
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
    //    if (sensors.isConversionAvailable(0)) {
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

  mqtt.loop();
  ArduinoOTA.handle();
}
