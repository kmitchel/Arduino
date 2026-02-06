#ifndef MPC_CONTROLLER_H
#define MPC_CONTROLLER_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"

// Temperature bins for learning
enum TempBin {
    BIN_BITTER = 0,  // < 10°F
    BIN_COLD,        // 10-25°F
    BIN_COOL,        // 25-40°F
    BIN_MILD,        // 40-55°F
    BIN_WARM,        // > 55°F
    BIN_COUNT
};

const char* BIN_NAMES[] = { "bitter", "cold", "cool", "mild", "warm" };
const char* BIN_LABELS[] = { "< 10°F", "10-25°F", "25-40°F", "40-55°F", "> 55°F" };
const int BIN_LIMITS[] = { 10, 25, 40, 55, 999 };

// MPC States
enum MPCState {
    STATE_BOOT,
    STATE_MAINTENANCE,
    STATE_RECOVERY,
    STATE_COAST,
    STATE_AWAY,
    STATE_FALLBACK,
    STATE_OVERRIDE
};

const char* STATE_NAMES[] = {
    "BOOT", "MAINTENANCE", "RECOVERY", "COAST", "AWAY", "FALLBACK", "OVERRIDE"
};

// Heat rate learning data
struct HeatRateData {
    float rate;
    int samples;
};

// Cycle tracking for learning
struct LearningCycle {
    bool active;
    unsigned long startTime;
    float startTemp;
    float outsideTemp;
};

class MPCController {
public:
    void begin();
    void update(float indoorTemp, float outsideTemp, bool isHome, 
                int sunriseHour, int sunriseMin, int sunsetHour, int sunsetMin);
    
    // State
    MPCState getState() { return state; }
    const char* getStateName() { return STATE_NAMES[state]; }
    bool shouldHeat() { return heatOn; }
    
    // Getters for monitoring
    float getTargetTemp() { return targetTemp; }
    float getDynamicCoast() { return dynamicCoast; }
    float getCurrentHeatRate();
    TempBin getCurrentBin() { return currentBin; }
    const char* getCurrentBinLabel() { return BIN_LABELS[currentBin]; }
    
    // Learning
    void startLearningCycle(float indoorTemp, float outsideTemp);
    void endLearningCycle(float endTemp);
    void saveBrain();
    void loadBrain();
    
    // Get learning data for MQTT
    String getHeatRatesJson();

private:
    MPCState state = STATE_BOOT;
    bool heatOn = false;
    float targetTemp = TEMP_COMFORT;
    float dynamicCoast = TEMP_COAST;
    TempBin currentBin = BIN_COOL;
    
    // Heat rate learning
    HeatRateData heatRates[BIN_COUNT];
    LearningCycle learningCycle;
    
    // Timing
    unsigned long lastStateChange = 0;
    unsigned long heatStartTime = 0;
    
    // Temperature history for rate-of-change
    float tempHistory[4];
    unsigned long tempHistoryTimes[4];
    int tempHistoryIndex = 0;
    
    // Helpers
    TempBin getTempBin(float outsideTemp);
    float getDynamicCoastTemp(float outsideTemp);
    float getTempDropRate();
    void trackTempHistory(float temp);
};

// ==========================================
// IMPLEMENTATION
// ==========================================

void MPCController::begin() {
    // Initialize heat rates with defaults
    for (int i = 0; i < BIN_COUNT; i++) {
        heatRates[i].rate = DEFAULT_HEAT_RATE;
        heatRates[i].samples = 0;
    }
    
    // Initialize temp history
    for (int i = 0; i < 4; i++) {
        tempHistory[i] = 0;
        tempHistoryTimes[i] = 0;
    }
    
    learningCycle.active = false;
    
    // Load saved brain
    loadBrain();
}

TempBin MPCController::getTempBin(float outsideTemp) {
    for (int i = 0; i < BIN_COUNT; i++) {
        if (outsideTemp < BIN_LIMITS[i]) {
            return (TempBin)i;
        }
    }
    return BIN_WARM;
}

float MPCController::getDynamicCoastTemp(float outsideTemp) {
    if (outsideTemp < 10) return 65.0f;   // Bitter: minimal setback
    if (outsideTemp < 25) return 64.0f;   // Cold: moderate
    if (outsideTemp < 40) return 62.0f;   // Cool: normal
    return 60.0f;                          // Mild+: deeper setback
}

float MPCController::getCurrentHeatRate() {
    HeatRateData& data = heatRates[currentBin];
    
    if (data.samples == 0) {
        // Try weighted average from other bins
        float totalRate = 0;
        int totalWeight = 0;
        for (int i = 0; i < BIN_COUNT; i++) {
            if (heatRates[i].samples > 0) {
                totalRate += heatRates[i].rate * heatRates[i].samples;
                totalWeight += heatRates[i].samples;
            }
        }
        if (totalWeight > 0) {
            return totalRate / totalWeight;
        }
        return DEFAULT_HEAT_RATE;
    }
    
    return data.rate;
}

void MPCController::trackTempHistory(float temp) {
    tempHistory[tempHistoryIndex] = temp;
    tempHistoryTimes[tempHistoryIndex] = millis();
    tempHistoryIndex = (tempHistoryIndex + 1) % 4;
}

float MPCController::getTempDropRate() {
    // Find oldest and newest valid entries
    int oldest = -1, newest = -1;
    unsigned long oldestTime = ULONG_MAX, newestTime = 0;
    
    for (int i = 0; i < 4; i++) {
        if (tempHistoryTimes[i] > 0) {
            if (tempHistoryTimes[i] < oldestTime) {
                oldestTime = tempHistoryTimes[i];
                oldest = i;
            }
            if (tempHistoryTimes[i] > newestTime) {
                newestTime = tempHistoryTimes[i];
                newest = i;
            }
        }
    }
    
    if (oldest < 0 || newest < 0 || oldest == newest) return 0;
    
    float hoursDiff = (newestTime - oldestTime) / 3600000.0f;
    if (hoursDiff < 0.25f) return 0;  // Need 15 mins of data
    
    float tempDiff = tempHistory[oldest] - tempHistory[newest];  // Positive = dropping
    return tempDiff / hoursDiff;
}

void MPCController::update(float indoorTemp, float outsideTemp, bool isHome,
                           int sunriseHour, int sunriseMin, int sunsetHour, int sunsetMin) {
    // Track outside temp history
    trackTempHistory(outsideTemp);
    
    // Update current bin
    currentBin = getTempBin(outsideTemp);
    
    // Calculate dynamic coast
    dynamicCoast = getDynamicCoastTemp(outsideTemp);
    
    // Calculate drop rate
    float dropRate = getTempDropRate();
    bool isRapidCooling = dropRate > TEMP_DROP_RATE_THRESHOLD;
    
    // Get current time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    int currentHour = timeinfo->tm_hour;
    int currentMin = timeinfo->tm_min;
    float currentTimeDecimal = currentHour + currentMin / 60.0f;
    
    // Calculate target and coast trigger times
    float sunriseDecimal = sunriseHour + sunriseMin / 60.0f;
    float sunsetDecimal = sunsetHour + sunsetMin / 60.0f;
    float coastTrigger = sunsetDecimal + (COAST_DELAY_AFTER_SUNSET_MIN / 60.0f);
    
    // MPC calculations
    float effectiveHeatRate = getCurrentHeatRate();
    float degreesNeeded = TEMP_COMFORT - indoorTemp;
    float hoursToHeat = degreesNeeded > 0 ? degreesNeeded / effectiveHeatRate : 0;
    float minutesToHeat = (hoursToHeat * 60) + SOAK_BUFFER_MIN;
    float triggerTime = sunriseDecimal - (minutesToHeat / 60.0f);
    
    // Determine comfort window
    bool isAfterSunrise = currentTimeDecimal >= sunriseDecimal;
    bool isBeforeCoast = currentTimeDecimal < coastTrigger;
    bool isComfortWindow = isAfterSunrise && isBeforeCoast && !isRapidCooling;
    
    // State machine
    MPCState prevState = state;
    
    // Presence override
    if (!isHome) {
        state = STATE_AWAY;
        targetTemp = dynamicCoast;
    }
    // Comfort window
    else if (isComfortWindow) {
        state = STATE_MAINTENANCE;
        targetTemp = TEMP_COMFORT;
    }
    // Recovery period (before sunrise)
    else if (currentTimeDecimal >= triggerTime && currentTimeDecimal < sunriseDecimal) {
        state = STATE_RECOVERY;
        targetTemp = TEMP_COMFORT;
    }
    // Coast
    else {
        state = STATE_COAST;
        targetTemp = dynamicCoast;
    }
    
    // Heat control with hysteresis
    if (indoorTemp < targetTemp - HEAT_ON_DELTA) {
        heatOn = true;
    } else if (indoorTemp > targetTemp + HEAT_OFF_DELTA) {
        heatOn = false;
    }
    
    // Safety: force off in AWAY/COAST if temp is ok
    if ((state == STATE_AWAY || state == STATE_COAST) && indoorTemp >= dynamicCoast) {
        heatOn = false;
    }
    
    // Log state changes
    if (state != prevState) {
        Serial.printf("[MPC] State: %s -> %s\n", STATE_NAMES[prevState], STATE_NAMES[state]);
        lastStateChange = millis();
    }
}

void MPCController::startLearningCycle(float indoorTemp, float outsideTemp) {
    learningCycle.active = true;
    learningCycle.startTime = millis();
    learningCycle.startTemp = indoorTemp;
    learningCycle.outsideTemp = outsideTemp;
    Serial.println("[Learning] Started cycle");
}

void MPCController::endLearningCycle(float endTemp) {
    if (!learningCycle.active) return;
    
    float durationHours = (millis() - learningCycle.startTime) / 3600000.0f;
    
    if (durationHours < 0.25f) {
        Serial.println("[Learning] Cycle too short, discarding");
        learningCycle.active = false;
        return;
    }
    
    if (endTemp <= learningCycle.startTemp) {
        Serial.println("[Learning] No temp rise, discarding");
        learningCycle.active = false;
        return;
    }
    
    float tempRise = endTemp - learningCycle.startTemp;
    float measuredRate = tempRise / durationHours;
    
    TempBin bin = getTempBin(learningCycle.outsideTemp);
    HeatRateData& data = heatRates[bin];
    
    // Weighted update
    float historyWeight = min(0.9f, 0.5f + (data.samples * 0.05f));
    float newWeight = 1.0f - historyWeight;
    
    data.rate = (data.rate * historyWeight) + (measuredRate * newWeight);
    data.samples++;
    
    Serial.printf("[Learning] Learned for %s: %.2f deg/hr (%d samples)\n",
        BIN_LABELS[bin], data.rate, data.samples);
    
    learningCycle.active = false;
    saveBrain();
}

void MPCController::saveBrain() {
    File file = SPIFFS.open("/thermal_brain.json", "w");
    if (!file) {
        Serial.println("[Brain] Failed to open file for writing");
        return;
    }
    
    JsonDocument doc;
    JsonObject rates = doc["heatRates"].to<JsonObject>();
    
    for (int i = 0; i < BIN_COUNT; i++) {
        JsonObject bin = rates[BIN_NAMES[i]].to<JsonObject>();
        bin["rate"] = heatRates[i].rate;
        bin["samples"] = heatRates[i].samples;
    }
    
    serializeJson(doc, file);
    file.close();
    Serial.println("[Brain] Saved to SPIFFS");
}

void MPCController::loadBrain() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[Brain] SPIFFS mount failed");
        return;
    }
    
    if (!SPIFFS.exists("/thermal_brain.json")) {
        Serial.println("[Brain] No saved brain found, using defaults");
        return;
    }
    
    File file = SPIFFS.open("/thermal_brain.json", "r");
    if (!file) {
        Serial.println("[Brain] Failed to open file");
        return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("[Brain] JSON parse failed");
        return;
    }
    
    JsonObject rates = doc["heatRates"];
    for (int i = 0; i < BIN_COUNT; i++) {
        if (rates[BIN_NAMES[i]].is<JsonObject>()) {
            heatRates[i].rate = rates[BIN_NAMES[i]]["rate"];
            heatRates[i].samples = rates[BIN_NAMES[i]]["samples"];
        }
    }
    
    Serial.println("[Brain] Loaded from SPIFFS:");
    for (int i = 0; i < BIN_COUNT; i++) {
        Serial.printf("  %s: %.2f deg/hr (%d samples)\n",
            BIN_LABELS[i], heatRates[i].rate, heatRates[i].samples);
    }
}

String MPCController::getHeatRatesJson() {
    String json = "{";
    for (int i = 0; i < BIN_COUNT; i++) {
        if (i > 0) json += ",";
        json += "\"";
        json += BIN_NAMES[i];
        json += "\":{\"rate\":";
        json += String(heatRates[i].rate, 2);
        json += ",\"samples\":";
        json += heatRates[i].samples;
        json += "}";
    }
    json += "}";
    return json;
}

#endif // MPC_CONTROLLER_H
