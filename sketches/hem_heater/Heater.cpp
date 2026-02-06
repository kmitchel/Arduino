#include "Heater.h"

Heater::Heater(uint8_t ssrPin, uint8_t ledPin) 
    : _ssrPin(ssrPin), _ledPin(ledPin), _isOn(false), _safetyLockout(false), _lastSwitchTime(0), _onSince(0) {}

void Heater::begin() {
    pinMode(_ssrPin, OUTPUT);
    pinMode(_ledPin, OUTPUT);
    digitalWrite(_ssrPin, LOW);
    digitalWrite(_ledPin, HIGH); // LED off (active low)
}

bool Heater::set(bool on) {
    if (_safetyLockout && on) {
        return false; // Prevent turning on if locked out
    }

    unsigned long now = millis();

    if (on && !_isOn) {
        // Trying to turn ON
        if (_lastSwitchTime > 0 && (now - _lastSwitchTime < MIN_OFF_TIME_MS)) {
            return false; // Too soon to turn on
        }
    } else if (!on && _isOn) {
        // Trying to turn OFF
        if (now - _lastSwitchTime < MIN_ON_TIME_MS) {
            return false; // Too soon to turn off
        }
    }

    if (on != _isOn) {
        _isOn = on;
        digitalWrite(_ssrPin, on ? HIGH : LOW);
        digitalWrite(_ledPin, on ? LOW : HIGH);
        _lastSwitchTime = now;
        
        if (on) {
            _onSince = now;
            Serial.println("Heater: ON");
        } else {
            Serial.println("Heater: OFF");
        }
        return true; // State changed
    }
    return false;
}

bool Heater::get() const {
    return _isOn;
}

void Heater::forceOff() {
    // Bypass min-times
    if (_isOn) {
        _isOn = false;
        digitalWrite(_ssrPin, LOW);
        digitalWrite(_ledPin, HIGH);
        _lastSwitchTime = millis();
        Serial.println("Heater: FORCED OFF");
    }
}

void Heater::resetLockout() {
    _safetyLockout = false;
    Serial.println("Heater: Lockout Cleared");
}

bool Heater::isLockedOut() const {
    return _safetyLockout;
}

bool Heater::update() {
    if (_isOn && (millis() - _onSince > SAFETY_TIMEOUT_MS)) {
        Serial.println("SAFETY: Timeout reached, forcing OFF and LATCHING");
        forceOff();
        _safetyLockout = true;
        return true; // Warning needed
    }
    return false;
}
