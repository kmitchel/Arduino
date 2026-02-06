#include "SensorManager.h"

SensorManager::SensorManager(uint8_t pin) 
    : _oneWire(pin), _sensors(&_oneWire), 
      _currentTemp(-999), _lastReadSuccessTime(0), _lastRequestTime(0), 
      _conversionInProgress(false), _consecutiveGoodReadings(0),
      _historyIndex(0), _historyFilled(false) {}

void SensorManager::begin() {
    _sensors.begin();
    _sensors.setWaitForConversion(false);
}

void SensorManager::update() {
    unsigned long now = millis();
    
    if (!_conversionInProgress && (now - _lastRequestTime > READ_INTERVAL_MS)) {
        _sensors.requestTemperatures();
        _lastRequestTime = now;
        _conversionInProgress = true;
    }
    else if (_conversionInProgress && (now - _lastRequestTime > 2000)) { // 2s wait
        _conversionInProgress = false;
        
        // Safety: Assume failure unless proven otherwise
        bool validRead = false;
        
        if (_sensors.getDeviceCount() > 0) {
            float temp = _sensors.getTempFByIndex(0);
            
            if (temp > -100 && temp < 185) {
                validRead = true;
                _consecutiveGoodReadings++;
                
                if (_consecutiveGoodReadings >= REQUIRED_GOOD_READINGS) {
                    _currentTemp = temp;
                    _lastReadSuccessTime = now;
                    recordTemperature(temp);
                }
            }
        }
        
        if (!validRead) {
             _consecutiveGoodReadings = 0;
             _currentTemp = -999; // Immediate invalidation
        }
    }
}

float SensorManager::getTemp() const {
    return _currentTemp;
}

bool SensorManager::isDataValid() const {
    // True only if we have a valid temp AND it's recent
    return (_currentTemp > -100) && (millis() - _lastReadSuccessTime < MAX_AGE_MS);
}

void SensorManager::recordTemperature(float temp) {
    _history[_historyIndex] = temp;
    _historyTimes[_historyIndex] = millis();
    _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
    if (_historyIndex == 0) _historyFilled = true;
}

float SensorManager::getRate() const {
    if (!_historyFilled && _historyIndex < 2) return 0;

    int oldestIdx = _historyFilled ? (_historyIndex + 1) % HISTORY_SIZE : 0;
    int newestIdx = (_historyIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    
    float oldestTemp = _history[oldestIdx];
    float newestTemp = _history[newestIdx];
    unsigned long oldestTime = _historyTimes[oldestIdx];
    unsigned long newestTime = _historyTimes[newestIdx];
    
    if (newestTime <= oldestTime) return 0;
    
    float minutesElapsed = (newestTime - oldestTime) / 60000.0;
    if (minutesElapsed < 0.5) return 0;
    
    return (newestTemp - oldestTemp) / minutesElapsed;
}
