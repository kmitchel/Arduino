#ifndef THERMOSTAT_H
#define THERMOSTAT_H

#include <Arduino.h>

enum ControlMode {
    MODE_OFF,
    MODE_ON,
    MODE_AUTO
};

class Thermostat {
public:
    Thermostat();
    // Added riseRate and isSensorValid parameters
    bool update(float currentTemp, float riseRate, bool isSensorValid, bool isPresence, bool isHvacActive);
    
    // Setters
    void setMode(ControlMode mode);
    void setSetpoint(float setpoint);
    
    // Getters
    ControlMode getMode() const;
    float getSetpoint() const;
    bool shouldHeat() const;

private:
    ControlMode _mode;
    float _setpoint;
    bool _shouldHeat;
    
    // PWM State
    unsigned long _pwmCycleStart;
    static const unsigned long PWM_CYCLE_MS = 60000;
};

#endif
