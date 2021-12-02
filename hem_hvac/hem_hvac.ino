#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <PubSubClient.h>

#include <Wire.h>

WiFiClient espClient;

PubSubClient mqtt(espClient);

const char* server = "raspberrypi";
const char* ssid     = "Mitchell";
const char* password = "easypassword";

const int addr = 0x39;

const uint8_t cool = 5;
const uint8_t fan = 6;
const uint8_t heat = 7;
const uint8_t coolOver = 2;
const uint8_t fanOver = 3;
const uint8_t heatOver = 4;

const uint8_t READY = 1, COOLON = 2, HEATON = 3, COOLING = 4, HEATING = 5, FANWAIT = 6, WAIT = 7, OFF = 8;
const uint8_t COOL = 1, HEAT = 2;
uint8_t state = READY, hvacMode = 2;
uint8_t heatSet = 68, coolSet = 70;

unsigned long stateDelay, machineDelay;

float tempF = 72.0;
float di = 70.0;

void callback(char* topic, byte* payload, unsigned int length) {
  String payloads;
  for (int i = 0; i < length; i++) {
    payloads += (char)payload[i];
  }

  if (strcmp(topic, "hvac/mode") == 0) {
    if (payloads == "off"){
      hvacMode = OFF;
    }
    else if (payloads == "cool") {
      hvacMode = COOL;
    }
    else if (payloads == "heat") {
      hvacMode = HEAT;
    }
  }
  if (strcmp(topic, "hvac/coolSet") == 0) {
    if (payloads == "?") {
      mqtt.publish("hvac/coolSet", String(coolSet).c_str());
    } else {
      int thisNumber = payloads.toInt();
      constrain(thisNumber, 60, 85);
      coolSet = thisNumber;
    }
  }
  if (strcmp(topic, "hvac/heatSet") == 0) {
    if (payloads == "?") {
      mqtt.publish("hvac/heatSet", String(heatSet).c_str());
    } else {
      int thisNumber = payloads.toInt();
      constrain(thisNumber, 60, 85);
        heatSet = thisNumber;
    }
  }
  if (strcmp(topic, "temp/tempF") == 0) {
    float thisNumber = payloads.toFloat();
    if (thisNumber > 0) {
//      tempF = .95 * tempF + .05 * thisNumber;
      tempF = thisNumber;
    }
//    mqtt.publish("test/tempF", String(tempF).c_str());
  }
  if (strcmp(topic, "temp/di") == 0) {
    float thisNumber = payloads.toFloat();
    if (thisNumber > 0) {
//      di = .96 * di + .04 * thisNumber;
      di = thisNumber;
    }
//    mqtt.publish("test/di", String(di).c_str());
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
    mqtt.subscribe("hvac/+");
    mqtt.subscribe("temp/tempF");
    mqtt.subscribe("temp/di");
    mqtt.subscribe("temp/289c653f03000027");
  }
}

void gpioWrite (uint8_t pin, uint8_t value) {
  uint8_t data;
  Wire.beginTransmission(addr);
  Wire.requestFrom(addr, 1);
  data = Wire.read();
  Wire.endTransmission();
  if (value == LOW)
  {
    data &= ~(1 << pin);
  }
  else
  {
    data |= (1 << pin);
  }
  Wire.beginTransmission(addr);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t gpioRead (uint8_t pin) {
  uint8_t data;
  Wire.beginTransmission(addr);
  Wire.requestFrom(addr, 1);
  data = Wire.read();
  Wire.endTransmission();
  return ((data >> pin) & 1);
}

void setup() {
  pinMode(2, OUTPUT);
  digitalWrite(2, 0);

  Wire.begin();

  Wire.beginTransmission(addr);
  Wire.write(255);
  Wire.endTransmission();


  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
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
  ArduinoOTA.setHostname("hvac");
  ArduinoOTA.begin();

}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }

  if (!mqtt.connected()) {
    mqttConnect();
  }

  if (millis() > machineDelay) {
    machineDelay = millis() + 15000;
    switch (state) {
      case READY:
        if (hvacMode == OFF) {
          mqtt.publish("hvac/state", "Off");
        } else if (hvacMode == COOL) {
          mqtt.publish("hvac/state", "CoolReady");
          if (di > coolSet + 1.5) {
            state = COOLON;
          }
        } else if (hvacMode == HEAT) {
          mqtt.publish("hvac/state", "HeatReady");
          if (tempF < heatSet - 0.75) {
            state = HEATON;
          }
        }
        break;
      case COOLON:
        gpioWrite(cool, LOW);
        gpioWrite(fan, LOW);
        mqtt.publish("hvac/state", "CoolOn");
        stateDelay = millis() + 450000;
        state = COOLING;
        gpioWrite(coolOver, LOW);
        gpioWrite(fanOver, LOW);
        gpioWrite(heatOver, LOW);
        break;
      case HEATON:
        gpioWrite(heat, LOW);
        mqtt.publish("hvac/state", "HeatOn");
        stateDelay = millis() + 600000;
        state = HEATING;
        gpioWrite(coolOver, LOW);
        gpioWrite(fanOver, LOW);
        gpioWrite(heatOver, LOW);
        break;
      case COOLING:
        mqtt.publish("hvac/state", "Cooling");
        if (di < coolSet - 1.5 && millis() > stateDelay) {
          stateDelay = millis() + 180000;
          state = FANWAIT;
          gpioWrite(cool, HIGH);
        }
        break;
      case HEATING:
        mqtt.publish("hvac/state", "Heating");
        if (tempF > heatSet + 2.25 && millis() > stateDelay) {
          stateDelay = millis() + 300000;
          state = WAIT;
          gpioWrite(heat, HIGH);
        }
        break;
      case FANWAIT:
        mqtt.publish("hvac/state", "FanWait");
        if (millis() > stateDelay) {
          stateDelay = millis() + 300000;
          state = WAIT;
          gpioWrite(fan, HIGH);
        }
        break;
      case WAIT:
        mqtt.publish("hvac/state", "Wait");
        if (millis() > stateDelay) {
          state = READY;
          gpioWrite(coolOver, HIGH);
          gpioWrite(fanOver, HIGH);
          gpioWrite(heatOver, HIGH);
        }
        break;
    }
  }
  mqtt.loop();
  ArduinoOTA.handle();
}
