#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// One Wire init straight from examples.
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 14
#define TEMPERATURE_PRECISION 12 //Fast conversion.
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#include <PubSubClient.h>
WiFiClient espClient;

PubSubClient mqtt(espClient);

const char* server = "raspberrypi";
const char* ssid     = "Mitchell";
const char* password = "easypassword";

//Time variables
unsigned long wNewTime, wOldTime, lastTemp, stateDelay;

boolean firstRun = true;
boolean wPulse = false;
boolean conversionInProgress = false;

#define W_SENSOR 12

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
    // Pulse less than 300 ms is > 24kW. Probably noise, better to just drop the pulse.
    if (wNewTime - wOldTime > 300) {
      //Ignore the first pulse. Need 2 good pulses to calc.
      if (!firstRun) {
        //Compute watts. No floating point necessary. 2Wh per pulse.
//        unsigned int currentW = 7200000.0 / (wNewTime - wOldTime);
        unsigned int currentW = 3600000.0 / (wNewTime - wOldTime);
        mqtt.publish("power/W", String(currentW).c_str());
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
  
  mqtt.loop();
  ArduinoOTA.handle();
}

