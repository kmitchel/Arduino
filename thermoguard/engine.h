#ifndef ENGINE_H
#define ENGINE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// User-facing Modes
enum ThermoMode {
    MODE_OFF,
    MODE_HEAT,
    MODE_COOL,
    MODE_AUTO,   
    MODE_FAN,
    MODE_COUNT
};

// Internal Logic States
enum ThermoState {
    STATE_IDLE,
    STATE_HEATING,
    STATE_COOLING,
    STATE_FAN_ON,
    STATE_WAIT_HEAT, 
    STATE_WAIT_COOL,
    STATE_COUNT
};

class ThermoEngine {
public:
    static void init();
    static void update();

    static void setMode(ThermoMode mode);
    static ThermoMode getMode();

    static void setTargetTemp(float tempF);
    static float getTargetTemp();

    static void provideRemoteTemp(float tempF);
    
    static ThermoState getState();
    
    // Updated to use JsonDocument reference
    static void populateStatusJson(JsonDocument& doc);

private:
    static float _getEffectiveTemp(); 
    static void _processLogic(float currentTemp);
    static void _changeState(ThermoState newState);
    static void _validateInternalState();
    static void _saveSettings();
    static void _loadSettings();
};

#endif
