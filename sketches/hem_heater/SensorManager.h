#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

class SensorManager {
public:
    SensorManager(uint8_t pin);
    void begin();
    void update();
    float getTemp() const;
    float getRate() const;
    
    // Safety
    bool isDataValid() const;
    
private:
    OneWire _oneWire;
    DallasTemperature _sensors;
    
    // State
    float _currentTemp;
    unsigned long _lastReadSuccessTime; // Time of last GOOD reading
    unsigned long _lastRequestTime;
    bool _conversionInProgress;
    
    // Safety
    int _consecutiveGoodReadings;
    static const int REQUIRED_GOOD_READINGS = 3;
    static const unsigned long READ_INTERVAL_MS = 15000;
    static const unsigned long MAX_AGE_MS = 45000;

    // Rate Calculation
    static const int HISTORY_SIZE = 4;
    float _history[HISTORY_SIZE];
    unsigned long _historyTimes[HISTORY_SIZE];
    int _historyIndex;
    bool _historyFilled;
    
    void recordTemperature(float temp);
};

#endif
