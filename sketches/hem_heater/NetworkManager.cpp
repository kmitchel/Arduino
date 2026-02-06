#include "NetworkManager.h"
#include "secrets.h"

NetworkManager::NetworkManager() : _mqtt(_espClient), _lastReconnectAttempt(0) {}

void NetworkManager::begin(MQTT_CALLBACK_SIGNATURE) {
    setupWifi();
    _mqtt.setBufferSize(1024);
    _mqtt.setServer(MQTT_SERVER, 1883);
    _mqtt.setCallback(callback);
    
    // OTA Setup
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.onStart([]() { Serial.println("OTA Start"); });
    ArduinoOTA.onEnd([]() { Serial.println("\nOTA End"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
}

void NetworkManager::setupWifi() {
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void NetworkManager::update() {
    if (WiFi.status() != WL_CONNECTED) {
        setupWifi();
    }
    
    if (!_mqtt.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = now;
            reconnectMqtt();
        }
    } else {
        _mqtt.loop();
    }
    
    ArduinoOTA.handle();
}

void NetworkManager::reconnectMqtt() {
    Serial.print("Attempting MQTT connection...");
    if (_mqtt.connect(HOSTNAME)) {
        Serial.println("connected");
        _mqtt.subscribe("heater/cmd");
        _mqtt.subscribe("heater/setpoint");
        _mqtt.subscribe("hvac/state");
        _mqtt.subscribe("state"); // Fallback
    } else {
        Serial.print("failed, rc=");
        Serial.print(_mqtt.state());
        Serial.println(" try again in 5 seconds");
    }
}

bool NetworkManager::connected() {
    return _mqtt.connected();
}

void NetworkManager::publish(const char* topic, const char* payload, bool retained) {
    if (_mqtt.connected()) {
        _mqtt.publish(topic, payload, retained);
    }
}
