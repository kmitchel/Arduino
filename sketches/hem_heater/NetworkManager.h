#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

class NetworkManager {
public:
    NetworkManager();
    void begin(MQTT_CALLBACK_SIGNATURE);
    void update();
    bool connected();
    void publish(const char* topic, const char* payload, bool retained = false);
    
private:
    void setupWifi();
    void reconnectMqtt();
    
    WiFiClient _espClient;
    PubSubClient _mqtt;
    unsigned long _lastReconnectAttempt;
};

#endif
