#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <PubSubClient.h>

#include <Wire.h>
#include <time.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

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
// OPERATIONAL & SAFETY VARIABLES
// ==========================================
unsigned long lastTempUpdate = 0;           // Last time we got a temperature
const unsigned long SENSOR_TIMEOUT = 300000; // 5 mins failsafe
unsigned long heatStartTime = 0;            // Tracking for max runtime
const unsigned long MAX_HEAT_TIME = 7200000; // 2 hours safety cutoff
bool failsafeActive = false;

// ==========================================
// SCHEDULER & PERSISTENCE
// ==========================================
struct ScheduleEntry {
  int hour;
  int temp;
};
ScheduleEntry schedule[3] = {
  {22, 60}, // 10 PM
  {0, 58},  // Midnight
  {6, 63}   // 6 AM
};
int currentScheduledSetpoint = -1;
unsigned long lastScheduleCheck = 0;
const char* tzConfig = "EST5EDT,M3.2.0,M11.1.0"; // US Eastern Time


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
      saveConfig();
    }
  }
  if (strcmp(topic, "hvac/schedule") == 0) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payloads);
    if (!error && doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      for (int i=0; i<arr.size() && i<3; i++) {
        schedule[i].hour = arr[i]["h"];
        schedule[i].temp = arr[i]["t"];
      }
      currentScheduledSetpoint = -1; // Force re-evaluation
      saveConfig();
      mqtt.publish("hvac/info", "Schedule updated via MQTT");
    }
  }
  if (strcmp(topic, "temp/tempF") == 0) {
    float thisNumber = payloads.toFloat();
    if (thisNumber > 0) {
      tempF = thisNumber;
      lastTempUpdate = millis(); // Refresh watchdog
      if (failsafeActive) {
        failsafeActive = false;
        mqtt.publish("hvac/info", "Failsafe cleared: Sensor data received");
      }
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
  configTime(0, 0, "192.168.1.1", "pool.ntp.org");
  setenv("TZ", tzConfig, 1);
  tzset();
}

void saveConfig() {
  StaticJsonDocument<512> doc;
  doc["heatSet"] = heatSet;
  doc["coolSet"] = coolSet;
  JsonArray sched = doc.createNestedArray("schedule");
  for (int i=0; i<3; i++) {
    JsonObject entry = sched.createNestedObject();
    entry["h"] = schedule[i].hour;
    entry["t"] = schedule[i].temp;
  }
  
  File file = LittleFS.open("/config.json", "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) return;
  File file = LittleFS.open("/config.json", "r");
  if (!file) return;
  
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (!error) {
    heatSet = doc["heatSet"] | heatSet;
    coolSet = doc["coolSet"] | coolSet;
    if (doc.containsKey("schedule")) {
      JsonArray sched = doc["schedule"];
      for (int i=0; i<sched.size() && i<3; i++) {
        schedule[i].hour = sched[i]["h"];
        schedule[i].temp = sched[i]["t"];
      }
    }
  }
  file.close();
}

void checkSchedule() {
  time_t now = time(nullptr);
  struct tm* ptm = localtime(&now);
  if (ptm->tm_year < 120) return; // Time not set yet

  int currentHour = ptm->tm_hour;
  int targetTemp = -1;
  
  // Find the most recent scheduled setpoint
  // We check in chronological order and take the latest one that has passed today
  // or the last one from yesterday if we are before the first one today.
  
  int bestMatchIdx = -1;
  int maxHourFound = -1;
  
  for(int i=0; i<3; i++) {
    if (currentHour >= schedule[i].hour) {
       if (schedule[i].hour > maxHourFound) {
         maxHourFound = schedule[i].hour;
         bestMatchIdx = i;
       }
    }
  }
  
  // If no match found today (e.g. before 6am), find the latest hour in the schedule (from yesterday)
  if (bestMatchIdx == -1) {
    for(int i=0; i<3; i++) {
      if (schedule[i].hour > maxHourFound) {
        maxHourFound = schedule[i].hour;
        bestMatchIdx = i;
      }
    }
  }

  if (bestMatchIdx != -1 && schedule[bestMatchIdx].temp != currentScheduledSetpoint) {
    currentScheduledSetpoint = schedule[bestMatchIdx].temp;
    heatSet = currentScheduledSetpoint;
    mqtt.publish("hvac/info", ("Schedule: Setpoint updated to " + String(heatSet)).c_str());
    saveConfig();
  }
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
  byte error = Wire.endTransmission();
  
  if (error != 0) {
    char errMsg[50];
    snprintf(errMsg, 50, "I2C Write Error: %d", error);
    mqtt.publish("hvac/error", errMsg);
  }
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

  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
  } else {
    loadConfig();
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }

  // ==========================================
  // SENSOR WATCHDOG - Failsafe logic
  // ==========================================
  if (!failsafeActive && (millis() - lastTempUpdate > SENSOR_TIMEOUT) && lastTempUpdate != 0) {
    failsafeActive = true;
    state = WAIT;
    stateDelay = millis() + 300000;
    gpioWrite(heat, HIGH); 
    gpioWrite(cool, HIGH);
    mqtt.publish("hvac/error", "FAILSAFE: Sensor data stale (>5m). Shutting down.");
  }


  if (!mqtt.connected()) {
    mqttConnect();
  }

  if (millis() - lastScheduleCheck > 60000) {
    lastScheduleCheck = millis();
    checkSchedule();
  }

  // Run State Machine Logic frequently (every 500ms)
  static unsigned long lastLogicTick = 0;
  if (millis() - lastLogicTick > 500) {
    lastLogicTick = millis();
    
    uint8_t oldState = state;
    
    switch (state) {
      case READY:
        if (hvacMode == OFF || failsafeActive) {
          // Handled in report
        } else if (hvacMode == COOL) {
          if (tempF > coolSet + 0.5) {
            state = COOLON;
          }
        } else if (hvacMode == HEAT) {
          if (tempF < heatSet - 0.5) {
            state = HEATON;
            heatStartTime = millis(); // Start safety timer
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
        if ((tempF > heatSet + 0.5 && millis() > stateDelay) || hvacMode != HEAT || failsafeActive) {
          stateDelay = millis() + 300000;
          state = WAIT;
          gpioWrite(heat, HIGH);
        } else if (millis() - heatStartTime > MAX_HEAT_TIME) {
          // Mandatory recovery period (15 mins)
          stateDelay = millis() + 900000; 
          state = WAIT;
          gpioWrite(heat, HIGH);
          mqtt.publish("hvac/error", "Safety Cutoff: 2h run limit reached. 15m rest initiated.");
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
      case WAIT:     stateStr = failsafeActive ? "Failsafe" : "Wait"; break;
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
