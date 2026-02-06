#include "Thermostat.h"

Thermostat::Thermostat() 
    : _mode(MODE_AUTO), _setpoint(75.0), _shouldHeat(false), _pwmCycleStart(0) {}

void Thermostat::setMode(ControlMode mode) {
    _mode = mode;
}

void Thermostat::setSetpoint(float setpoint) {
    if (setpoint >= 50 && setpoint <= 90) {
        _setpoint = setpoint;
    }
}

ControlMode Thermostat::getMode() const {
    return _mode;
}

float Thermostat::getSetpoint() const {
    return _setpoint;
}

bool Thermostat::shouldHeat() const {
    return _shouldHeat;
}

bool Thermostat::update(float currentTemp, float riseRate, bool isSensorValid, bool isPresence, bool isHvacActive) {
    // Global Strict Safety Checks (Override ALL modes)
    if (!isSensorValid) {
        Serial.println("SAFETY: Invalid/Stale sensor data - OFF");
        _shouldHeat = false;
        return false;
    }
    if (!isPresence) {
        // Strict: Never heat when AWAY, even in Manual mode
        _shouldHeat = false; 
        return false;
    }
    if (isHvacActive) {
        // Strict: Never heat when HVAC is ON (Inhibition)
        _shouldHeat = false;
        return false;
    }
    
    // If strict safety passes, proceed with mode logic
    switch (_mode) {
        case MODE_OFF:
            _shouldHeat = false;
            break;
            
        case MODE_ON:
            _shouldHeat = true;
            break;
            
        case MODE_AUTO:
            // PWM Control Logic
            { // Scoped block for vars
                float error = _setpoint - currentTemp;
                float dutyCycle = 0.0;

                if (error > 1.5) dutyCycle = 1.0;
                else if (error > 1.0) dutyCycle = 1.0;  // Stay full power longer
                else if (error > 0.5) dutyCycle = 0.75; // Boosted from 0.5
                else if (error > 0.2) dutyCycle = 0.50; // Boosted from 0.25
                else if (error > 0.0) dutyCycle = 0.25; // Boosted from 0.125
                else dutyCycle = 0.0;
                
                // Overshoot Prevention: If heating fast and close to setpoint, throttle back
                if (riseRate > 0.2 && error < 0.5) {
                    dutyCycle *= 0.5;
                }
                


                unsigned long now = millis();
                if (now - _pwmCycleStart >= PWM_CYCLE_MS) {
                    _pwmCycleStart = now;
                }
                
                _shouldHeat = (now - _pwmCycleStart) < (dutyCycle * PWM_CYCLE_MS);
            }
            break;
    }
    return _shouldHeat;
}
