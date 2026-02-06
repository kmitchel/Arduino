/*
 * hem_heater.ino - Refactored Space Heater Controller
 */

#include "Heater.h"
#include "Thermostat.h"
#include "SensorManager.h"
#include "NetworkManager.h"

// --- Configuration & Constants ---

// Hardware Pins
#define ONE_WIRE_BUS 12   // DS18B20 data pin
#define SSR_PIN 14        // SSR control pin (GPIO14 / D5)
#define LED_PIN 2         // Onboard LED (active low)

// Timing
const unsigned long DATA_TIMEOUT_MS = 180000;         // 2 minutes for fail-safe
const unsigned long STATE_PUBLISH_INTERVAL_MS = 30000;
const unsigned long WARNING_THROTTLE_MS = 10000;

// MQTT Topics
const char* TOPIC_CMD = "heater/cmd";
const char* TOPIC_SETPOINT = "heater/setpoint";
const char* TOPIC_MODE = "heater/mode";
const char* TOPIC_STATE = "heater/state";
const char* TOPIC_TEMP = "heater/temp";
const char* TOPIC_SETPOINT_CURRENT = "heater/setpoint/current";
const char* TOPIC_WARNING = "heater/warning";
const char* TOPIC_HVAC_STATE = "hvac/state";

// Warning Messages
const char* MSG_LOCKOUT = "Safety Lockout (Timeout)";
const char* MSG_SENSOR_INVALID = "Sensor Invalid";
const char* MSG_PRESENCE_AWAY = "Presence Timeout (AWAY)";
const char* MSG_HVAC_ACTIVE = "HVAC Timeout/Active";
const char* MSG_LOCKOUT_CLEARED = "Safety Lockout Cleared";
const char* MSG_LATCHED_OFF = "Safety Timeout - LATCHED OFF";

// --- Global Objects ---
Heater heater(SSR_PIN, LED_PIN);
Thermostat thermostat;
SensorManager sensors(ONE_WIRE_BUS);
NetworkManager network;

// --- Helper Structs ---
struct SystemState {
    bool isPresence;
    bool isHvacActive;
    unsigned long lastPresenceUpdate;
    unsigned long lastHvacUpdate;
};

// Initialize with 0 to force immediate timeout check at boot (Safe Default)
SystemState sysState = { false, true, 0, 0 };

unsigned long lastStatePublish = 0;

// --- Helper Functions ---

const char* getSafeWarning(bool isSensorValid, bool safePresence, bool safeHvacActive, bool isLockedOut) {
    if (isLockedOut) return MSG_LOCKOUT;
    if (!isSensorValid) return MSG_SENSOR_INVALID;
    if (!safePresence) return MSG_PRESENCE_AWAY;
    if (safeHvacActive) return MSG_HVAC_ACTIVE;
    return nullptr;
}

// Minimal JSON Value Finder (Robust to spacing)
// Returns pointer to start of value (after colon/quotes)
char* findJsonValueStart(char* json, const char* key) {
    char* pos = strstr(json, key);
    if (!pos) return nullptr;
    
    pos += strlen(key); // Skip key
    
    // Scan for colon
    while (*pos && *pos != ':' && *pos != '}' && *pos != ',') pos++;
    if (*pos == ':') pos++; // Skip colon
    
    // Skip whitespace and leading quotes
    while (*pos && (*pos == ' ' || *pos == '"')) pos++;
    
    return pos;
}

void publishState() {
    // Mode
    const char* modeStr;
    switch (thermostat.getMode()) {
        case MODE_OFF:  modeStr = "OFF"; break;
        case MODE_ON:   modeStr = "MANUAL"; break;
        case MODE_AUTO: modeStr = "AUTO"; break;
    }
    network.publish(TOPIC_MODE, modeStr, true);
    
    // Heater State
    network.publish(TOPIC_STATE, heater.get() ? "ON" : "OFF", true);
    
    // Setpoint
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", thermostat.getSetpoint());
    network.publish(TOPIC_SETPOINT_CURRENT, buf, true);
    
    // Temperature
    if (sensors.isDataValid()) {
        snprintf(buf, sizeof(buf), "%.1f", sensors.getTemp());
        network.publish(TOPIC_TEMP, buf);
    }
}

// --- MQTT Callback ---
static char callbackBuffer[512]; 

void callback(char* topic, byte* payload, unsigned int length) {
    if (length >= sizeof(callbackBuffer)) length = sizeof(callbackBuffer) - 1;
    memcpy(callbackBuffer, payload, length);
    callbackBuffer[length] = '\0';
    
    // 1. Commands (Heater On/Off/Auto or Reset)
    if (strcmp(topic, TOPIC_CMD) == 0) {
        if (strcmp(callbackBuffer, "RESET") == 0) {
            heater.resetLockout();
            network.publish(TOPIC_WARNING, MSG_LOCKOUT_CLEARED, true);
            publishState();
            return;
        }
        if (strcmp(callbackBuffer, "OFF") == 0) {
            heater.resetLockout();
            thermostat.setMode(MODE_OFF);
            publishState();
            return;
        }
        
        // Commands below DO NOT clear lockout
        if (strcmp(callbackBuffer, "ON") == 0) thermostat.setMode(MODE_ON);
        else if (strcmp(callbackBuffer, "AUTO") == 0) thermostat.setMode(MODE_AUTO);
        
        network.publish(TOPIC_MODE, callbackBuffer, true);
        publishState();
    }
    
    // 2. Setpoint Update
    else if (strcmp(topic, TOPIC_SETPOINT) == 0) {
        float sp = atof(callbackBuffer);
        thermostat.setSetpoint(sp);
        network.publish(TOPIC_SETPOINT_CURRENT, callbackBuffer, true);
        publishState();
    }
    
    // 3. MPC State Update ("state")
    else if (strcmp(topic, "state") == 0) {
        // Presence ("presence":"HOME")
        char* val = findJsonValueStart(callbackBuffer, "\"presence\"");
        if (val) {
            if (strncmp(val, "HOME", 4) == 0) sysState.isPresence = true;
            else sysState.isPresence = false;
            sysState.lastPresenceUpdate = millis();
        }
        
        // Heat Command ("heatCommand":true/false)
        val = findJsonValueStart(callbackBuffer, "\"heatCommand\"");
        if (val) {
            if (strncmp(val, "true", 4) == 0) sysState.isHvacActive = true;
            else sysState.isHvacActive = false;
            sysState.lastHvacUpdate = millis();
        }
        
        Serial.printf("MPC State -> Presence: %s, Cmd: %s\n", 
            sysState.isPresence ? "HOME" : "AWAY", 
            sysState.isHvacActive ? "ON" : "OFF");
    }
    
    // 4. Furnace State Update ("hvac/state")
    // Strings like "Heating", "Reference", "Idle", etc.
    else if (strcmp(topic, TOPIC_HVAC_STATE) == 0) {
        bool isFurnaceActive = (strncmp(callbackBuffer, "Heating", 7) == 0) || 
                               (strncmp(callbackBuffer, "HeatOn", 6) == 0);
                               
        if (isFurnaceActive) {
            sysState.isHvacActive = true;
            sysState.lastHvacUpdate = millis(); // Trust furnace activity as "network alive" assurance
            Serial.println("Furnace Active (Override)");
        }
        // If furnace is Idle, we DO NOT force sysState.isHvacActive to false here,
        // because MPC might still be commanding heat (e.g. pre-ignition).
        // relying on MPC "heatCommand" for the OFF state is safer.
        
        Serial.printf("Furnace State: %s\n", callbackBuffer);
    }
}

// --- Main Setup & Loop ---

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== Refactored Space Heater Controller ===");
    
    heater.begin();
    sensors.begin();
    network.begin(callback);
    
    Serial.println("System Initialized");
}

void loop() {
    network.update();
    sensors.update();
    
    // 1. Gather Sensor Data
    float currentTemp = sensors.getTemp();
    float riseRate = sensors.getRate();
    bool isSensorValid = sensors.isDataValid();
    
    // 2. Determine Fail-Safe State
    // Default to strict safety if data is stale
    bool safePresence = sysState.isPresence;
    bool safeHvacActive = sysState.isHvacActive;
    unsigned long now = millis();
    
    if (now - sysState.lastPresenceUpdate > DATA_TIMEOUT_MS) {
        safePresence = false; // Force AWAY
    }
    
    if (now - sysState.lastHvacUpdate > DATA_TIMEOUT_MS) {
        safeHvacActive = true; // Force HVAC ON (Inhibit heater)
    }

    // 3. Safety Check & Warnings
    // Runs unconditionally to ensure visibility
    bool isLockedOut = heater.isLockedOut();
    const char* currentWarning = getSafeWarning(isSensorValid, safePresence, safeHvacActive, isLockedOut);

    static unsigned long lastWarningTime = 0;
    static const char* lastWarningMsg = nullptr; // Pointer comparison logic

    if (currentWarning) {
         // Immediate if changed, otherwise throttled
         bool isNew = (lastWarningMsg != currentWarning); // Safe pointer comparison for literals
         // Redundant strcmp removed as requested - all warnings are shared literals
         
         bool timeToPublish = (now - lastWarningTime > WARNING_THROTTLE_MS);

         if (isNew || timeToPublish) {
             network.publish(TOPIC_WARNING, currentWarning, true);
             lastWarningTime = now;
             lastWarningMsg = currentWarning;
         }
    } else {
        lastWarningMsg = nullptr;
    }

    // 4. Control Logic
    bool shouldHeat = false;
    
    if (!isLockedOut) {
        shouldHeat = thermostat.update(currentTemp, riseRate, isSensorValid, safePresence, safeHvacActive);
    } 
    // else shouldHeat remains false (Enforced Lockout)

    // 5. Apply Output
    if (heater.set(shouldHeat)) {
        publishState();
    }
    
    // Latch Check (Post-update)
    if (heater.update()) {
        network.publish(TOPIC_WARNING, MSG_LOCKOUT, true); // Consistent with getSafeWarning
        lastWarningMsg = MSG_LOCKOUT; // Align with main warning loop
        lastWarningTime = now;
    }
    
    // 6. Periodic Reporting
    if (now - lastStatePublish > STATE_PUBLISH_INTERVAL_MS) {
        publishState();
        lastStatePublish = now;
    }
}
