#include "engine.h"
#include "hal.h"
#include "config.h"
#include <ArduinoJson.h>

// ==========================================
// Globals
// ==========================================
ThermoMode _currentMode = MODE_OFF;
ThermoState _currentState = STATE_IDLE;
Preferences _prefs;
unsigned long _lastSaveRequest = 0;
bool _savePending = false;

float _targetTemp = 68.0;      
float _remoteTemp = NAN;
unsigned long _lastRemoteUpdate = 0;
const unsigned long REMOTE_TIMEOUT = 300000; 
// Tracks time in current state. Resets on ANY state change, including to WAIT states.
// This means limits apply to continuous state time, not aggregate runtime.
unsigned long _stateStartTime = 0;

unsigned long _lastLogicRun = 0;
const unsigned long LOGIC_INTERVAL = 1000; 

// ==========================================
// Implementation
// ==========================================

void ThermoEngine::init() {
    Serial.println("[Engine] Initializing Logic...");
    _loadSettings();
    _stateStartTime = millis();
}

void ThermoEngine::update() {
    _validateInternalState();
    HAL::ping(); // Heartbeat to prevent HAL failsafe
    unsigned long now = millis();
    if (now - _lastLogicRun < LOGIC_INTERVAL) return;
    _lastLogicRun = now;
    
    // Save settings if pending and debounced
    if (_savePending && (now - _lastSaveRequest > 2000)) {
        _saveSettings();
    }

    // 1. Get Temperature
    float currentTemp = _getEffectiveTemp();

    // 2. Validate Input
    if (isnan(currentTemp)) {
        if (_currentMode != MODE_OFF && _currentMode != MODE_FAN) {
             if (_currentState != STATE_IDLE) {
                 Serial.println("[Engine] Invalid Temp (NAN). Forcing IDLE.");
                 _changeState(STATE_IDLE);
             }
        }
        return; 
    }

    // 3. Max Run Time Safety
    if (_currentState == STATE_HEATING || _currentState == STATE_COOLING) {
         if (now - _stateStartTime > MAX_RUN_TIME_MS) {
             Serial.println("[Engine] MAX_RUN_TIME Exceeded! Forcing IDLE.");
             _changeState(STATE_IDLE);
             return; 
         }
    }

    // 4. Run Logic
    _processLogic(currentTemp);
}

void ThermoEngine::setMode(ThermoMode mode) {
    if (_currentMode != mode) {
        Serial.printf("[Engine] Mode Change: %d -> %d\n", _currentMode, mode);
        _currentMode = mode;
        
        // Force state reset to IDLE on ANY mode change (safest)
        _changeState(STATE_IDLE);
        
        _savePending = true;
        _lastSaveRequest = millis();

        // Force immediate logic re-eval
        _lastLogicRun = 0; 
    }
}

ThermoMode ThermoEngine::getMode() { return _currentMode; }
void ThermoEngine::setTargetTemp(float tempF) { 
    if (abs(_targetTemp - tempF) > 0.01) {
        _targetTemp = tempF; 
        _savePending = true;
        _lastSaveRequest = millis();
    }
}
float ThermoEngine::getTargetTemp() { return _targetTemp; }

void ThermoEngine::provideRemoteTemp(float tempF) {
    _remoteTemp = tempF;
    _lastRemoteUpdate = millis();
}

ThermoState ThermoEngine::getState() { return _currentState; }

float ThermoEngine::_getEffectiveTemp() {
    if (millis() - _lastRemoteUpdate < REMOTE_TIMEOUT && !isnan(_remoteTemp) && _remoteTemp > -50) {
        return _remoteTemp;
    }
    return HAL::readTempF(); 
}

void ThermoEngine::_changeState(ThermoState newState) {
    if (_currentState == newState) return;
    
    Serial.printf("[Engine] State Transition: %d -> %d\n", _currentState, newState);
    
    // Attempt Transitions
    ThermoState effectiveState = newState;

    // Reset runtime counter.
    // NOTE: This resets even if transition is blocked (e.g. going to WAIT state).
    // This means MAX_RUN_TIME_MS tracks continuous time in a specific state,
    // not just the total time trying to heat/cool.
    _stateStartTime = millis(); 

    switch (newState) {
        case STATE_IDLE:
        case STATE_WAIT_HEAT:
        case STATE_WAIT_COOL:
            HAL::setRelay(RELAY_HEAT, false);
            HAL::setRelay(RELAY_COOL, false);
            HAL::setRelay(RELAY_FAN, false);
            break;
            
        case STATE_HEATING:
             if (!HAL::setRelay(RELAY_HEAT, true)) {
                 Serial.println("[Engine] HAL Blocked Heat. Holding in WAIT.");
                 effectiveState = STATE_WAIT_HEAT; 
             } else {
                 HAL::setRelay(RELAY_COOL, false);
                 HAL::setRelay(RELAY_FAN, false); 
             }
            break;
            
        case STATE_COOLING:
             if (!HAL::setRelay(RELAY_COOL, true)) {
                 Serial.println("[Engine] HAL Blocked Cool. Holding in WAIT.");
                 effectiveState = STATE_WAIT_COOL;
             } else {
                 HAL::setRelay(RELAY_HEAT, false);
                 HAL::setRelay(RELAY_FAN, true); 
             }
            break;
            
        case STATE_FAN_ON:
            HAL::setRelay(RELAY_HEAT, false);
            HAL::setRelay(RELAY_COOL, false);
            HAL::setRelay(RELAY_FAN, true);
            break;
    }

    _currentState = effectiveState;
}

void ThermoEngine::_processLogic(float t) {
    if (_currentMode == MODE_OFF) {
        if (_currentState != STATE_IDLE) _changeState(STATE_IDLE);
        return;
    }
    
    if (_currentMode == MODE_FAN) {
        if (_currentState != STATE_FAN_ON) _changeState(STATE_FAN_ON);
        return;
    }

    // HEAT MODE
    if (_currentMode == MODE_HEAT) {
        if (_currentState == STATE_IDLE || _currentState == STATE_WAIT_HEAT) {
            if (t <= _targetTemp - HYSTERESIS_ON) _changeState(STATE_HEATING);
        }
        else if (_currentState == STATE_HEATING) {
            if (t >= _targetTemp + HYSTERESIS_OFF) _changeState(STATE_IDLE);
        }
        else {
            // Mismatched state for HEAT mode
            _changeState(STATE_IDLE);
        }
    }
    // COOL MODE
    else if (_currentMode == MODE_COOL) {
        if (_currentState == STATE_IDLE || _currentState == STATE_WAIT_COOL) {
            if (t >= _targetTemp + HYSTERESIS_ON) _changeState(STATE_COOLING);
        }
        else if (_currentState == STATE_COOLING) {
            if (t <= _targetTemp - HYSTERESIS_OFF) _changeState(STATE_IDLE);
        }
        else {
            // Mismatched state for COOL mode
            _changeState(STATE_IDLE);
        }
    }
    // AUTO MODE
    else if (_currentMode == MODE_AUTO) {
        float heatTarget = _targetTemp - (AUTO_DEADBAND / 2.0);
        float coolTarget = _targetTemp + (AUTO_DEADBAND / 2.0);

        if (_currentState == STATE_IDLE || _currentState == STATE_WAIT_HEAT || _currentState == STATE_WAIT_COOL) {
            if (t <= heatTarget - HYSTERESIS_ON) {
                _changeState(STATE_HEATING);
            } else if (t >= coolTarget + HYSTERESIS_ON) {
                _changeState(STATE_COOLING);
            }
        }
        else if (_currentState == STATE_HEATING) {
            if (t >= heatTarget + HYSTERESIS_OFF) _changeState(STATE_IDLE);
        }
        else if (_currentState == STATE_COOLING) {
            if (t <= coolTarget - HYSTERESIS_OFF) _changeState(STATE_IDLE);
        }
    }
}

void ThermoEngine::populateStatusJson(JsonDocument& doc) {
    doc["mode"] = (int)_currentMode;
    doc["state"] = (int)_currentState;
    doc["target"] = _targetTemp;
    
    float et = _getEffectiveTemp();
    if (isnan(et)) doc["effTemp"] = nullptr;
    else doc["effTemp"] = et;
    
    doc["usingRemote"] = (millis() - _lastRemoteUpdate < REMOTE_TIMEOUT && !isnan(_remoteTemp));
}
    
void ThermoEngine::_validateInternalState() {
    if (_currentMode < 0 || _currentMode >= MODE_COUNT) {
        Serial.println("[Engine] ERROR: Corrupt Mode detected! Resetting to OFF.");
        _currentMode = MODE_OFF;
    }
    if (_currentState < 0 || _currentState >= STATE_COUNT) {
        Serial.println("[Engine] ERROR: Corrupt State detected! Resetting to IDLE.");
        _currentState = STATE_IDLE;
    }
}

void ThermoEngine::_loadSettings() {
    _prefs.begin("thermo", true); // Read-only
    _targetTemp = _prefs.getFloat("target", 68.0);
    _currentMode = (ThermoMode)_prefs.getInt("mode", (int)MODE_OFF);
    _prefs.end();
    
    Serial.printf("[Engine] Settings Loaded: Mode %d, Target %.1f\n", _currentMode, _targetTemp);
}

void ThermoEngine::_saveSettings() {
    _prefs.begin("thermo", false); // Read-write
    _prefs.putFloat("target", _targetTemp);
    _prefs.putInt("mode", (int)_currentMode);
    _prefs.end();
    
    _savePending = false;
    Serial.println("[Engine] Settings Saved to NVS.");
}
