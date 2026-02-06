#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <Arduino.h>
#include "config.h"

class RelayControl {
public:
    void begin();
    
    // Individual relay control
    void setHeat(bool on);
    void setCool(bool on);
    void setFan(bool on);
    void setHeatOverride(bool on);
    void setFanOverride(bool on);
    void setCoolOverride(bool on);
    
    // Bulk operations
    void allOff();
    void allOverridesOff();
    
    // Status
    bool isHeatOn() { return heatOn; }
    bool isCoolOn() { return coolOn; }
    bool isFanOn() { return fanOn; }
    
    // Get relay state as bitmask for MQTT
    uint8_t getStateBitmask();

private:
    bool heatOn = false;
    bool coolOn = false;
    bool fanOn = false;
    bool heatOverrideOn = false;
    bool fanOverrideOn = false;
    bool coolOverrideOn = false;
    
    void writeRelay(uint8_t pin, bool on);
};

// Implementation
void RelayControl::begin() {
    // Configure all relay pins as outputs
    pinMode(PIN_RELAY_HEAT, OUTPUT);
    pinMode(PIN_RELAY_COOL, OUTPUT);
    pinMode(PIN_RELAY_FAN, OUTPUT);
    pinMode(PIN_RELAY_HEAT_OVERRIDE, OUTPUT);
    pinMode(PIN_RELAY_FAN_OVERRIDE, OUTPUT);
    pinMode(PIN_RELAY_COOL_OVERRIDE, OUTPUT);
    
    // Start with all relays OFF (HIGH for active-low)
    allOff();
    allOverridesOff();
}

void RelayControl::writeRelay(uint8_t pin, bool on) {
    // Active LOW: LOW = relay ON, HIGH = relay OFF
    digitalWrite(pin, on ? LOW : HIGH);
}

void RelayControl::setHeat(bool on) {
    if (heatOn != on) {
        heatOn = on;
        writeRelay(PIN_RELAY_HEAT, on);
        Serial.printf("[Relay] Heat: %s\n", on ? "ON" : "OFF");
    }
}

void RelayControl::setCool(bool on) {
    if (coolOn != on) {
        coolOn = on;
        writeRelay(PIN_RELAY_COOL, on);
        Serial.printf("[Relay] Cool: %s\n", on ? "ON" : "OFF");
    }
}

void RelayControl::setFan(bool on) {
    if (fanOn != on) {
        fanOn = on;
        writeRelay(PIN_RELAY_FAN, on);
        Serial.printf("[Relay] Fan: %s\n", on ? "ON" : "OFF");
    }
}

void RelayControl::setHeatOverride(bool on) {
    if (heatOverrideOn != on) {
        heatOverrideOn = on;
        writeRelay(PIN_RELAY_HEAT_OVERRIDE, on);
    }
}

void RelayControl::setFanOverride(bool on) {
    if (fanOverrideOn != on) {
        fanOverrideOn = on;
        writeRelay(PIN_RELAY_FAN_OVERRIDE, on);
    }
}

void RelayControl::setCoolOverride(bool on) {
    if (coolOverrideOn != on) {
        coolOverrideOn = on;
        writeRelay(PIN_RELAY_COOL_OVERRIDE, on);
    }
}

void RelayControl::allOff() {
    setHeat(false);
    setCool(false);
    setFan(false);
}

void RelayControl::allOverridesOff() {
    setHeatOverride(false);
    setFanOverride(false);
    setCoolOverride(false);
}

uint8_t RelayControl::getStateBitmask() {
    uint8_t state = 0;
    if (heatOn) state |= (1 << 5);
    if (fanOn) state |= (1 << 4);
    if (coolOn) state |= (1 << 3);
    if (heatOverrideOn) state |= (1 << 2);
    if (fanOverrideOn) state |= (1 << 1);
    if (coolOverrideOn) state |= (1 << 0);
    return state;
}

#endif // RELAY_CONTROL_H
