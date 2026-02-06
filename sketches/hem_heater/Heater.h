#ifndef HEATER_H
#define HEATER_H

#include <Arduino.h>

class Heater {
public:
    Heater(uint8_t ssrPin, uint8_t ledPin);
    void begin();
    
    // Returns TRUE if state changed
    bool set(bool on);
    
    // Returns TRUE if device is ON
    bool get() const;
    
    // Returns TRUE if safety timeout triggered (warning needed)
    bool update(); 
    
    // Safety
    void forceOff();      // Bypass min-times for safety shutdown
    void resetLockout();  // Clear safety lockout (Requires explicit OFF/RESET)
    bool isLockedOut() const; // Returns true if safety timeout occurred, latching OFF

    // Configuration
    static const unsigned long SAFETY_TIMEOUT_MS = 3600000; // 1 hour
    static const unsigned long MIN_OFF_TIME_MS = 5000;
    static const unsigned long MIN_ON_TIME_MS = 5000;

private:
    uint8_t _ssrPin;
    uint8_t _ledPin;
    bool _isOn;
    bool _safetyLockout;
    unsigned long _lastSwitchTime; // Tracks last ON or OFF switch times
    unsigned long _onSince;        // Tracks when it turned ON for safety timeout
};

#endif
