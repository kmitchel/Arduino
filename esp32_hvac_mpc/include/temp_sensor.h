#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

class TempSensor {
public:
    void begin();
    bool read();  // Returns true if successful
    
    float getTempF() { return tempF; }
    float getTempC() { return tempC; }
    bool isValid() { return valid; }
    unsigned long getLastReadTime() { return lastReadTime; }
    
    // Get sensor address as string
    String getAddressString();

private:
    OneWire oneWire;
    DallasTemperature sensors;
    DeviceAddress sensorAddress;
    
    float tempF = 0.0f;
    float tempC = 0.0f;
    bool valid = false;
    bool initialized = false;
    unsigned long lastReadTime = 0;
    bool addressFound = false;
};

// Implementation
void TempSensor::begin() {
    oneWire = OneWire(PIN_DS18B20);
    sensors = DallasTemperature(&oneWire);
    sensors.begin();
    
    // Find the first sensor
    if (sensors.getDeviceCount() > 0) {
        if (sensors.getAddress(sensorAddress, 0)) {
            addressFound = true;
            Serial.print("[TempSensor] Found DS18B20 at: ");
            Serial.println(getAddressString());
            
            // Set resolution (9-12 bits, higher = slower but more precise)
            sensors.setResolution(sensorAddress, 12);
        }
    }
    
    if (!addressFound) {
        Serial.println("[TempSensor] WARNING: No DS18B20 found!");
    }
}

bool TempSensor::read() {
    if (!addressFound) {
        valid = false;
        return false;
    }
    
    sensors.requestTemperatures();
    
    float newTempC = sensors.getTempC(sensorAddress);
    
    // Check for error values
    if (newTempC == DEVICE_DISCONNECTED_C || newTempC < -55.0f || newTempC > 125.0f) {
        Serial.println("[TempSensor] Invalid reading, sensor may be disconnected");
        valid = false;
        return false;
    }
    
    
    if (!initialized) {
        tempC = newTempC;
        initialized = true;
    } else {
        // Exponential Moving Average
        tempC = (TEMP_FILTER_ALPHA * newTempC) + ((1.0f - TEMP_FILTER_ALPHA) * tempC);
    }
    
    tempF = DallasTemperature::toFahrenheit(tempC);
    valid = true;
    lastReadTime = millis();
    
    return true;
}

String TempSensor::getAddressString() {
    if (!addressFound) return "NOT_FOUND";
    
    String addr = "";
    for (int i = 0; i < 8; i++) {
        if (sensorAddress[i] < 16) addr += "0";
        addr += String(sensorAddress[i], HEX);
    }
    return addr;
}

#endif // TEMP_SENSOR_H
