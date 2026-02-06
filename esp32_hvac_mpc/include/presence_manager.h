#ifndef PRESENCE_MANAGER_H
#define PRESENCE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <ESP32Ping.h>
#include "config.h"

// Phone structure
struct Phone {
    const char* name;
    const char* mac;
    const char* ip;
    bool detectedBT;
    bool detectedIP;
    bool isHome;
};

class PresenceManager {
public:
    void begin();
    void scan();
    
    bool isAnyoneHome() { return anyoneHome; }
    bool isHome(int phoneIndex);
    String getStatusString();
    
    // Get detection details for MQTT
    String getDetailsJson();

private:
    BLEScan* pBLEScan = nullptr;
    
    Phone phones[2] = {
        { BT_PHONE_1_NAME, BT_PHONE_1_MAC, BT_PHONE_1_IP, false, false, false },
        { BT_PHONE_2_NAME, BT_PHONE_2_MAC, BT_PHONE_2_IP, false, false, false }
    };
    
    bool anyoneHome = true;  // Assume home until proven otherwise
    int consecutiveAllFails = 0;
    int consecutiveAnySuccess = 0;
    
    unsigned long lastScanTime = 0;
    
    // Convert MAC string to lowercase for comparison
    String normalizeMac(String mac);
};

// Implementation
void PresenceManager::begin() {
    // Initialize BLE
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    Serial.println("[Presence] BLE scanner initialized");
    Serial.printf("[Presence] Tracking: %s (%s / %s), %s (%s / %s)\n",
        phones[0].name, phones[0].mac, phones[0].ip,
        phones[1].name, phones[1].mac, phones[1].ip);
}

String PresenceManager::normalizeMac(String mac) {
    mac.toLowerCase();
    return mac;
}

void PresenceManager::scan() {
    Serial.println("[Presence] Starting hybrid scan...");
    
    // 1. Bluetooth Scan
    if (pBLEScan) {
        // Reset BT flags
        for (int i = 0; i < 2; i++) phones[i].detectedBT = false;
        
        // BLEScanResults returns by value in ESP32 Arduino 2.x
        BLEScanResults foundDevices = pBLEScan->start(5, false);
        int count = foundDevices.getCount();
        Serial.printf("[Presence] BLE found %d devices\n", count);
        
        for (int i = 0; i < count; i++) {
            BLEAdvertisedDevice device = foundDevices.getDevice(i);
            String foundMac = normalizeMac(String(device.getAddress().toString().c_str()));
            
            for (int p = 0; p < 2; p++) {
                if (foundMac == normalizeMac(String(phones[p].mac))) {
                    phones[p].detectedBT = true;
                    Serial.printf("[Presence] BLE found %s!\n", phones[p].name);
                }
            }
        }
        pBLEScan->clearResults();
    }
    
    // 2. IP Ping Scan
    for (int i = 0; i < 2; i++) {
        phones[i].detectedIP = false;
        if (Ping.ping(phones[i].ip, 1)) {
            phones[i].detectedIP = true;
            Serial.printf("[Presence] IP ping success for %s!\n", phones[i].name);
        }
    }
    
    // 3. Combine Results
    bool currentSomeoneHome = false;
    for (int i = 0; i < 2; i++) {
        // Home if EITHER method succeeds
        bool rawDetected = phones[i].detectedBT || phones[i].detectedIP;
        phones[i].isHome = rawDetected; // Instant update for individual status
        
        if (rawDetected) currentSomeoneHome = true;
    }
    
    // 4. Global Hysteresis
    if (currentSomeoneHome) {
        consecutiveAllFails = 0;
        consecutiveAnySuccess++;
        
        if (!anyoneHome && consecutiveAnySuccess >= PRESENCE_HOME_THRESHOLD) {
            anyoneHome = true;
            Serial.println("[Presence] *** WELCOME HOME! ***");
        }
    } else {
        consecutiveAnySuccess = 0;
        consecutiveAllFails++;
        
        if (anyoneHome && consecutiveAllFails >= PRESENCE_AWAY_THRESHOLD) {
            anyoneHome = false;
            Serial.println("[Presence] *** EVERYONE LEFT! ***");
        }
    }
    
    lastScanTime = millis();
}

bool PresenceManager::isHome(int phoneIndex) {
    if (phoneIndex < 0 || phoneIndex >= 2) return false;
    return phones[phoneIndex].isHome;
}

String PresenceManager::getStatusString() {
    String status = "";
    for (int i = 0; i < 2; i++) {
        if (i > 0) status += ", ";
        status += phones[i].name;
        status += ": ";
        status += phones[i].isHome ? "HOME" : "AWAY";
        
        status += " (";
        status += phones[i].detectedBT ? "BT" : "-";
        status += "/";
        status += phones[i].detectedIP ? "IP" : "-";
        status += ")";
    }
    return status;
}

String PresenceManager::getDetailsJson() {
    String json = "{";
    for (int i = 0; i < 2; i++) {
        if (i > 0) json += ",";
        json += "\"";
        json += phones[i].name;
        json += "\":{\"home\":";
        json += phones[i].isHome ? "true" : "false";
        json += ",\"bt\":";
        json += phones[i].detectedBT ? "true" : "false";
        json += ",\"ip\":";
        json += phones[i].detectedIP ? "true" : "false";
        json += "}";
    }
    json += "}";
    return json;
}

#endif // PRESENCE_MANAGER_H
