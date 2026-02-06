#include <Arduino.h>
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "config.h"
#include "hal.h"
#include "engine.h"
#include "web.h"
#include "pins.h"

// ==========================================
// Setup
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n\n");
    Serial.println("==================================");
    Serial.printf("   %s Firmware v0.1\n", PROJECT_NAME);
    Serial.println("==================================");

    // 1. Initialize Hardware Abstraction Layer
    HAL::init();
    
    // 1b. Initialize Engine
    ThermoEngine::init();
    ThermoEngine::setMode(MODE_OFF); // Safety default

    // 2. Connect WiFi (Temporary Blocking for Phase 1 clarity)
    Serial.printf("[WiFi] Connecting to %s...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    // Wait for connection with simple timeout
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
        HAL::feedWatchdog(); // Don't crash while connecting
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Connected!");
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        
        // 3. Setup mDNS
        if (MDNS.begin(HOSTNAME)) {
            Serial.printf("[mDNS] Responder active: %s.local\n", HOSTNAME);
        }

        // 3b. Start Web Server
        WebManager::init();

        // 4. Setup OTA
        ArduinoOTA.setHostname(HOSTNAME);
        ArduinoOTA.setPassword(OTA_PASSWORD);
        ArduinoOTA.onStart([]() {
            String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
            Serial.println("Start updating " + type);
            HAL::allOff(); // Safety First!
        });
        ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("Error[%u]\n", error);
        });
        ArduinoOTA.begin();
    } else {
        Serial.println("\n[WiFi] Connection Failed. Continuing in Offline Mode.");
    }
}

// ==========================================
// Main Loop
// ==========================================
void loop() {
    // 1. Hardware Updates (Sensors, Safety)
    HAL::update();
    HAL::feedWatchdog();

    // 2. Logic Engine
    ThermoEngine::update();

    // 3. Network Services
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }

    // 3. Phase 1 Test Logic: Blink Status LED
    static unsigned long lastBlink = 0;
    static bool ledState = false;
    if (millis() - lastBlink > 1000) {
        lastBlink = millis();
        ledState = !ledState;
        digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
        
        // Print Status
        // Serial.println(HAL::getSystemStatus());
    }

    // 4. Yield to RTOS
    delay(1); 
}
