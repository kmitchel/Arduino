#include "hal.h"
#include "pins.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <esp_task_wdt.h>
#include <esp_arduino_version.h>

// ==========================================
// Globals
// ==========================================
OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);

bool _relayState[RELAY_COUNT] = {false};
unsigned long _relayLastOffTime[RELAY_COUNT] = {0}; 
unsigned long _relayStartTime[RELAY_COUNT] = {0};   
unsigned long _lastCommandTime = 0; 
bool _failsafeTriggered = false; // Track active failsafe state

float _lastTempF = NAN; 
unsigned long _lastTempRead = 0;
bool _sensorValid = false;
const unsigned long TEMP_READ_INTERVAL = 5000; 
#define WDT_TIMEOUT 8 

const int _relayPins[RELAY_COUNT] = {
    PIN_RELAY_HEAT, PIN_RELAY_COOL, PIN_RELAY_FAN,
    PIN_OVERRIDE_HEAT, PIN_OVERRIDE_FAN, PIN_OVERRIDE_COOL
};

// ==========================================
// Implementation
// ==========================================

void HAL::init() {
    Serial.println("[HAL] Initializing Hardware...");

    for (int i = 0; i < RELAY_COUNT; i++) {
        // Recommendation 3: Boot safety check
        // Check if relay is active (LOW for active-low) before we take control
        if (digitalRead(_relayPins[i]) == LOW) {
            Serial.printf("[HAL] WARNING: Relay %d was ON at boot! Forcing OFF.\n", i);
        }
        pinMode(_relayPins[i], OUTPUT);
        _writeRelay(_relayPins[i], false); 
        _relayState[i] = false;
        _relayLastOffTime[i] = millis(); 
    }
    _lastCommandTime = millis();

    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW); 

    sensors.begin();
    sensors.setWaitForConversion(false); 
    sensors.requestTemperatures();       
    
    Serial.println("[HAL] Enabling Watchdog...");
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_init(&twdt_config);
#else
    esp_task_wdt_init(WDT_TIMEOUT, true); 
#endif
    esp_task_wdt_add(NULL);

    Serial.println("[HAL] Hardware Ready.");
}

void HAL::ping() {
    _lastCommandTime = millis();
}

void HAL::update() {
    unsigned long now = millis();

    // 1. Failsafe Check
    if (now - _lastCommandTime > FAILSAFE_TIMEOUT_MS) {
        if (!_failsafeTriggered) {
            Serial.println("[HAL] Failsafe Triggered: No Logic Command. Forcing OFF.");
            _failsafeTriggered = true;
            allOff();
        }
        // Prevent log spam, but keep measuring duration if needed
        // We do NOT reset _lastCommandTime here, we rely on setRelay to reset it
        // when logic comes back alive.
        
        // Re-enforce OFF if relays somehow stuck? 
        // For now, assume allOff() worked.
    } else {
        _failsafeTriggered = false;
    }

    // 2. Temperature Reading
    if (now - _lastTempRead >= TEMP_READ_INTERVAL) {
        _lastTempRead = now;
        
        float t = sensors.getTempFByIndex(0);
        
        // Basic range check
        if (t > -50.0 && t < 150.0 && t != 185.0 && t != -196.0 && t != -127.0) {
            // Recommendation 5: Rate-of-change sanity check
            // Only apply if we already have a valid reading
            if (_sensorValid && !isnan(_lastTempF)) {
                float delta = abs(t - _lastTempF);
                if (delta > 5.0) { // 5 degree jump in 5 seconds is highly unlikely for ambient air
                    Serial.printf("[HAL] Sensor Sanity Fail: Jumped %.2f -> %.2f. Ignoring.\n", _lastTempF, t);
                    // We don't mark invalid yet, but we don't update _lastTempF either.
                    // This allows for a single glitch to be ignored.
                } else {
                    _lastTempF = t;
                    _sensorValid = true;
                }
            } else {
                // First valid reading
                _lastTempF = t;
                _sensorValid = true;
            }
        } else {
            Serial.printf("[HAL] Sensor Error: %.2f\n", t);
            _sensorValid = false; 
            _lastTempF = NAN; 
        }
        sensors.requestTemperatures();
    }
}

void HAL::feedWatchdog() {
    esp_task_wdt_reset();
}

void HAL::_writeRelay(int pin, bool active) {
    digitalWrite(pin, active ? LOW : HIGH);
}

bool HAL::_interlockCheck(RelayID relay, bool state) {
    if (!state) return true; 

    if (relay == RELAY_HEAT && _relayState[RELAY_COOL]) return false;
    if (relay == RELAY_COOL && _relayState[RELAY_HEAT]) return false;
    
    if (relay == RELAY_OVR_HEAT && _relayState[RELAY_OVR_COOL]) return false;
    if (relay == RELAY_OVR_COOL && _relayState[RELAY_OVR_HEAT]) return false;

    return true; 
}

bool HAL::_shortCycleCheck(RelayID relay, bool state) {
    if (!state) return true;
    
    // Skip Fan Relay for user comfort
    if (relay == RELAY_FAN || relay == RELAY_OVR_FAN) return true;

    unsigned long offDuration = millis() - _relayLastOffTime[relay];
    
    if (offDuration < MIN_OFF_TIME_MS) {
        unsigned long wait = (MIN_OFF_TIME_MS - offDuration) / 1000;
        Serial.printf("[HAL] Short-Cycle: Wait %lu s for Relay %d\n", wait, relay);
        return false;
    }
    return true;
}

bool HAL::setRelay(RelayID relay, bool state) {
    _lastCommandTime = millis(); 

    if (relay >= RELAY_COUNT) return false;
    if (_relayState[relay] == state) return true;

    if (state) {
        if (!_interlockCheck(relay, true)) {
            Serial.printf("[HAL] Interlock blocked Relay %d\n", relay);
            return false;
        }
        if (!_shortCycleCheck(relay, true)) {
            return false;
        }
    }

    _relayState[relay] = state;
    _writeRelay(_relayPins[relay], state);
    
    if (state) {
        _relayStartTime[relay] = millis();
    } else {
        _relayLastOffTime[relay] = millis();
    }
    
    return true;
}

bool HAL::getRelay(RelayID relay) {
    if (relay >= RELAY_COUNT) return false;
    return _relayState[relay];
}

void HAL::allOff() {
    for (int i = 0; i < RELAY_COUNT; i++) {
        _relayState[i] = false;
        _writeRelay(_relayPins[i], false);
        _relayLastOffTime[i] = millis();
    }
    // Recommendation 2: Update last command time to prevent immediate re-trigger of failsafe
    _lastCommandTime = millis(); 
    Serial.println("[HAL] Emergency ALL OFF");
}

float HAL::readTempF() { return _lastTempF; }
bool HAL::isSensorValid() { return _sensorValid; }

void HAL::populateStatusJson(JsonObject& doc) {
    doc["uptime"] = millis() / 1000;
    doc["failsafe"] = _failsafeTriggered;
    
    if (isnan(_lastTempF)) doc["temp"] = nullptr;
    else doc["temp"] = _lastTempF;
    
    doc["sensorOk"] = _sensorValid;
    
    JsonArray relays = doc.createNestedArray("relays");
    for(int i=0; i<RELAY_COUNT; i++) {
        relays.add(_relayState[i] ? 1 : 0);
    }
}
