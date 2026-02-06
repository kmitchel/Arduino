/*
 * ESP32 HVAC MPC Controller
 * 
 * Replaces ESP8266 + I2C expander with direct GPIO control.
 * Features:
 * - DS18B20 temperature sensor
 * - Direct relay control (6 relays)
 * - MPC logic with learning
 * - Bluetooth presence detection
 * - MQTT publishing for monitoring
 * - Weather API for sunrise/sunset
 * - Optional web dashboard
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <time.h>

#include "config.h"
#include "relay_control.h"
#include "temp_sensor.h"
#include "presence_manager.h"
#include "mpc_controller.h"
#include "web_dashboard.h"

// ==========================================
// GLOBAL OBJECTS
// ==========================================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

RelayControl relays;
TempSensor tempSensor;
PresenceManager presence;
MPCController mpc;
WebDashboard dashboard;

// ==========================================
// STATE VARIABLES
// ==========================================
float outsideTemp = 50.0f;  // Weather API result
int sunriseHour = TARGET_HOUR, sunriseMin = 0;
int sunsetHour = MAINT_END_HOUR, sunsetMin = 0;

unsigned long lastMpcLoop = 0;
unsigned long lastTempRead = 0;
unsigned long lastWeatherFetch = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastBtScan = 0;

// ==========================================
// WIFI CONNECTION
// ==========================================
void connectWifi() {
    Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Connection failed!");
    }
}

// ==========================================
// MQTT CONNECTION
// ==========================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.printf("[MQTT] Received: %s = %s\n", topic, message.c_str());
    
    // Handle commands here if needed
    // e.g., manual override, mode change, etc.
}

void connectMqtt() {
    if (mqtt.connected()) return;
    
    Serial.println("[MQTT] Connecting...");
    
    String clientId = "hvac_esp32_";
    clientId += String(random(0xffff), HEX);
    
    if (mqtt.connect(clientId.c_str())) {
        Serial.println("[MQTT] Connected!");
        mqtt.subscribe("hvac/+");
    } else {
        Serial.printf("[MQTT] Failed, rc=%d\n", mqtt.state());
    }
}

void publishState() {
    if (!mqtt.connected()) return;
    
    JsonDocument doc;
    doc["mode"] = mpc.getStateName();
    doc["temp"] = tempSensor.getTempF();
    doc["outside"] = outsideTemp;
    doc["target"] = mpc.getTargetTemp();
    doc["heatRate"] = mpc.getCurrentHeatRate();
    doc["tempBin"] = mpc.getCurrentBinLabel();
    doc["presence"] = presence.isAnyoneHome() ? "HOME" : "AWAY";
    doc["heatOn"] = mpc.shouldHeat();
    doc["dynamicCoast"] = mpc.getDynamicCoast();
    doc["sunrise"] = String(sunriseHour) + ":" + (sunriseMin < 10 ? "0" : "") + String(sunriseMin);
    doc["sunset"] = String(sunsetHour) + ":" + (sunsetMin < 10 ? "0" : "") + String(sunsetMin);
    doc["relayState"] = relays.getStateBitmask();
    
    String payload;
    serializeJson(doc, payload);
    mqtt.publish("state", payload.c_str());
    
    // Also publish individual topics
    mqtt.publish("hvac/state", relays.isHeatOn() ? "Heating" : "HeatReady");
    mqtt.publish("temp/tempF", String(tempSensor.getTempF(), 2).c_str());
}

// ==========================================
// WEATHER API (Open-Meteo)
// ==========================================
void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    Serial.println("[Weather] Fetching from Open-Meteo...");
    
    HTTPClient http;
    String url = "https://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + String(LOCATION_LAT, 4);
    url += "&longitude=" + String(LOCATION_LON, 4);
    url += "&current=temperature_2m&daily=sunrise,sunset";
    url += "&temperature_unit=fahrenheit&timezone=auto";
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error) {
            outsideTemp = doc["current"]["temperature_2m"];
            Serial.printf("[Weather] Outside temp: %.1f°F\n", outsideTemp);
            
            // Parse sunrise/sunset
            const char* sunrise = doc["daily"]["sunrise"][0];
            const char* sunset = doc["daily"]["sunset"][0];
            
            if (sunrise && strlen(sunrise) > 11) {
                sunriseHour = atoi(sunrise + 11);
                sunriseMin = atoi(sunrise + 14);
                Serial.printf("[Weather] Sunrise: %d:%02d\n", sunriseHour, sunriseMin);
            }
            
            if (sunset && strlen(sunset) > 11) {
                sunsetHour = atoi(sunset + 11);
                sunsetMin = atoi(sunset + 14);
                Serial.printf("[Weather] Sunset: %d:%02d\n", sunsetHour, sunsetMin);
            }
        }
    } else {
        Serial.printf("[Weather] HTTP error: %d\n", httpCode);
    }
    
    http.end();
}

// ==========================================
// OTA SETUP
// ==========================================
void setupOTA() {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("Start updating " + type);
        // Turn off all relays during update for safety
        relays.allOff();
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

    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");
}

// ==========================================
// SETUP
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("==============================================");
    Serial.println("   ESP32 HVAC MPC Controller");
    Serial.println("==============================================");
    
    // Initialize components
    relays.begin();
    tempSensor.begin();
    mpc.begin();
    SPIFFS.begin(); // Initialize SPIFFS for dashboard and config
    
    // Connect WiFi
    // Connect WiFi
    connectWifi();
    
    // Setup mDNS
    if (MDNS.begin(HOSTNAME)) {
        Serial.println("[MDNS] Responder started: " HOSTNAME ".local");
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("arduino", "tcp", 3232);
    }
    
    // Setup OTA
    setupOTA();
    
    // Initialize Dashboard (needs WiFi)
    dashboard.begin();
    
    // Configure time (for MPC scheduling)
    configTime(-5 * 3600, 3600, "pool.ntp.org");  // EST with DST
    
    // Setup MQTT
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    
    // Initialize Bluetooth
    presence.begin();
    
    // Initial weather fetch
    fetchWeather();
    
    // Initial temperature read
    tempSensor.read();
    
    Serial.println("[Init] Startup complete!");
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
    unsigned long now = millis();
    
    // WiFi reconnect
    if (WiFi.status() != WL_CONNECTED) {
        connectWifi();
    }
    
    // MQTT reconnect and loop
    if (!mqtt.connected()) {
        connectMqtt();
    }
    mqtt.loop();
    
    // OTA Handle
    ArduinoOTA.handle();
    
    // Temperature reading
    if (now - lastTempRead >= TEMP_READ_INTERVAL) {
        lastTempRead = now;
        if (tempSensor.read()) {
            Serial.printf("[Temp] Indoor: %.2f°F\n", tempSensor.getTempF());
        }
    }
    
    // Weather fetch
    if (now - lastWeatherFetch >= WEATHER_FETCH_INTERVAL) {
        lastWeatherFetch = now;
        fetchWeather();
    }
    
    // Bluetooth presence scan
    if (now - lastBtScan >= BT_SCAN_INTERVAL) {
        lastBtScan = now;
        presence.scan();
        Serial.printf("[Presence] %s\n", presence.getStatusString().c_str());
    }
    
    // MPC Logic
    if (now - lastMpcLoop >= MPC_LOOP_INTERVAL) {
        lastMpcLoop = now;
        
        Serial.println("========== MPC TICK ==========");
        
        // Update MPC
        mpc.update(
            tempSensor.getTempF(),
            outsideTemp,
            presence.isAnyoneHome(),
            sunriseHour, sunriseMin,
            sunsetHour, sunsetMin
        );
        
        // Apply relay state
        bool shouldHeat = mpc.shouldHeat();
        
        // Learning: detect start of heat cycle
        if (shouldHeat && !relays.isHeatOn()) {
            mpc.startLearningCycle(tempSensor.getTempF(), outsideTemp);
        }
        // Learning: detect end of heat cycle
        if (!shouldHeat && relays.isHeatOn()) {
            mpc.endLearningCycle(tempSensor.getTempF());
        }
        
        relays.setHeat(shouldHeat);
        
        Serial.printf("[MPC] State: %s, Target: %.1f°F, Heat: %s\n",
            mpc.getStateName(),
            mpc.getTargetTemp(),
            shouldHeat ? "ON" : "OFF");
        
        Serial.println("==============================\n");
    }
    
    // MQTT state publish
    if (now - lastMqttPublish >= MQTT_PUBLISH_INTERVAL) {
        lastMqttPublish = now;
        publishState();
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}
