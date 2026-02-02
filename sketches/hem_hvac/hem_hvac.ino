#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <PubSubClient.h>

#include <Wire.h>

WiFiClient espClient;

PubSubClient mqtt(espClient);

const char* server = "192.168.1.2";
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

unsigned long stateDelay, machineDelay, heartbeatDelay;

float tempF = 72.0;
float di = 70.0;

// ==========================================
// OPERATIONAL VARIABLES
// ==========================================


void callback(char* topic, byte* payload, unsigned int length) {
  String payloads;
  for (int i = 0; i < length; i++) {
    payloads += (char)payload[i];
  }



  // ==========================================
  // Original topic handlers
  // ==========================================
  
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
      coolSet = constrain(payloads.toInt(), 60, 85);
    }
  }
  if (strcmp(topic, "hvac/heatSet") == 0) {
    if (payloads == "?") {
      mqtt.publish("hvac/heatSet", String(heatSet).c_str());
    } else {
      heatSet = constrain(payloads.toInt(), 60, 85);
    }
  }
  if (strcmp(topic, "temp/tempF") == 0) {
    float thisNumber = payloads.toFloat();
    if (thisNumber > 0) {
      tempF = thisNumber;
    }
  }
  if (strcmp(topic, "temp/di") == 0) {
    float thisNumber = payloads.toFloat();
    if (thisNumber > 0) {
      di = thisNumber;
    }
  }
}

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("hvac");
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
  }
}

void gpioWrite (uint8_t pin, uint8_t value) {
  uint8_t data;
  Wire.requestFrom(addr, 1);
  if (Wire.available()) {
    data = Wire.read();
  } else {
    return;
  }
  
  if (value == LOW) {
    data &= ~(1 << pin);
  } else {
    data |= (1 << pin);
  }
  
  Wire.beginTransmission(addr);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t gpioRead (uint8_t pin) {
  uint8_t data = 0;
  Wire.requestFrom(addr, 1);
  if (Wire.available()) {
    data = Wire.read();
  }
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



  // Run State Machine Logic frequently (every 500ms)
  static unsigned long lastLogicTick = 0;
  if (millis() - lastLogicTick > 500) {
    lastLogicTick = millis();
    
    uint8_t oldState = state;
    
    switch (state) {
      case READY:
        if (hvacMode == OFF) {
          // Handled in report
        } else if (hvacMode == COOL) {
          if (tempF > coolSet + 0.5) {
            state = COOLON;
          }
        } else if (hvacMode == HEAT) {
          if (tempF < heatSet - 0.5) {
            state = HEATON;
          }
        }
        break;
      case COOLON:
        gpioWrite(cool, LOW);
        gpioWrite(fan, LOW);
        stateDelay = millis() + 450000;
        state = COOLING;
        gpioWrite(coolOver, LOW);
        gpioWrite(fanOver, LOW);
        gpioWrite(heatOver, LOW);
        break;
      case HEATON:
        gpioWrite(heat, LOW);
        stateDelay = millis() + 600000;
        state = HEATING;
        gpioWrite(coolOver, LOW);
        gpioWrite(fanOver, LOW);
        gpioWrite(heatOver, LOW);
        break;
      case COOLING:
        if ((tempF < coolSet - 0.5 && millis() > stateDelay) || hvacMode != COOL) {
          stateDelay = millis() + 180000;
          state = FANWAIT;
          gpioWrite(cool, HIGH);
        }
        break;
      case HEATING:
        if ((tempF > heatSet + 0.5 && millis() > stateDelay) || hvacMode != HEAT ) {
          stateDelay = millis() + 300000;
          state = WAIT;
          gpioWrite(heat, HIGH);
        }
        break;
      case FANWAIT:
        if (millis() > stateDelay) {
          stateDelay = millis() + 300000;
          state = WAIT;
          gpioWrite(fan, HIGH);
        }
        break;
      case WAIT:
        if (millis() > stateDelay) {
          state = READY;
          gpioWrite(coolOver, HIGH);
          gpioWrite(fanOver, HIGH);
          gpioWrite(heatOver, HIGH);
        }
        break;
    }

    // Force an immediate status update if state changed
    if (state != oldState) machineDelay = 0;
  }

  // Periodic Status Reporting (every 15s)
  if (millis() > machineDelay) {
    machineDelay = millis() + 15000;
    
    String stateStr = "Unknown";
    switch (state) {
      case READY:    stateStr = (hvacMode == OFF) ? "Off" : (hvacMode == COOL ? "CoolReady" : "HeatReady"); break;
      case COOLON:   stateStr = "CoolOn"; break;
      case HEATON:   stateStr = "HeatOn"; break;
      case COOLING:  stateStr = "Cooling"; break;
      case HEATING:  stateStr = "Heating"; break;
      case FANWAIT:  stateStr = "FanWait"; break;
      case WAIT:     stateStr = "Wait"; break;
    }
    mqtt.publish("hvac/state", stateStr.c_str());
    
    // Publish Relay State (Bitmask)
    uint8_t relayState = 0;
    relayState |= (gpioRead(heat) << 5);
    relayState |= (gpioRead(fan) << 4);
    relayState |= (gpioRead(cool) << 3);
    relayState |= (gpioRead(heatOver) << 2);
    relayState |= (gpioRead(fanOver) << 1);
    relayState |= (gpioRead(coolOver) << 0);
    mqtt.publish("hvac/relays", String(relayState).c_str());
    

  }

  // Heartbeat (every 30s)
  if (millis() > heartbeatDelay) {
    heartbeatDelay = millis() + 30000;
    mqtt.publish("hvac/heartbeat", String(millis()).c_str());
  }
  mqtt.loop();
  ArduinoOTA.handle();
}
