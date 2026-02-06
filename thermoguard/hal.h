#ifndef HAL_H
#define HAL_H

#include <Arduino.h>
#include <ArduinoJson.h>

enum RelayID {
    RELAY_HEAT,
    RELAY_COOL,
    RELAY_FAN,
    RELAY_OVR_HEAT,
    RELAY_OVR_FAN,
    RELAY_OVR_COOL,
    RELAY_COUNT
};

class HAL {
public:
    static void init();
    static void update();
    static void ping(); // Heartbeat from logic layer

    // Returns true if setting state was successful (passed safety checks)
    static bool setRelay(RelayID relay, bool state);
    static bool getRelay(RelayID relay);
    static void allOff(); 

    // Returns NAN if invalid
    static float readTempF();
    static bool isSensorValid();

    static void feedWatchdog();
    
    static void populateStatusJson(JsonObject& doc);

private:
    static void _writeRelay(int pin, bool active);
    static bool _interlockCheck(RelayID relay, bool state);
    static bool _shortCycleCheck(RelayID relay, bool state);
};

#endif
