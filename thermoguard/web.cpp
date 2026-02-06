#include "web.h"
#include "hal.h"
#include "engine.h"
#include "config.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

AsyncWebServer server(80);

void WebManager::init() {
    Serial.println("[Web] Initializing Web Server...");
    
    if (!SPIFFS.begin(true)) {
        Serial.println("[Web] SPIFFS Mount Failed! UI will be unavailable.");
        return;
    }

    _setupRoutes();
    server.begin();
    Serial.print("[Web] Server active on port 80. Open http://");
    Serial.println(WiFi.localIP());
}

void WebManager::_setupRoutes() {
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // API: Get Status
    server.on("/api/status", HTTP_GET, _handleAPIStatus);

    // API: Set Mode
    server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        
        if (index != 0 || len != total) {
            request->send(400, "application/json", "{\"error\":\"Chunked/Partial requests not supported\"}");
            return;
        }

        JsonDocument doc; // Small doc for simple commands
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        if (doc["mode"].is<int>()) {
            int m = doc["mode"];
            if (m >= 0 && m <= 4) { 
                ThermoEngine::setMode((ThermoMode)m);
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid Mode Range\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"Missing/Invalid 'mode' field\"}");
        }
    });

    // API: Set Target Temp
    server.on("/api/target", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        
        if (index != 0 || len != total) {
            request->send(400, "application/json", "{\"error\":\"Chunked/Partial requests not supported\"}");
            return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);

        if (error) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        if (doc["temp"].is<float>() || doc["temp"].is<int>()) {
            float t = doc["temp"];
            if (t >= 50.0 && t <= 90.0) {
                ThermoEngine::setTargetTemp(t);
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(400, "application/json", "{\"error\":\"Target out of safe range (50-90F)\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"Missing/Invalid 'temp' field\"}");
        }
    });
}

void WebManager::_handleAPIStatus(AsyncWebServerRequest *request) {
    // 1024 bytes should be enough for our status JSON
    JsonDocument doc; // ArduinoJson v7 handles memory automatically
    
    ThermoEngine::populateStatusJson(doc);
    
    doc["ip"] = WiFi.localIP().toString();
    doc["hostname"] = HOSTNAME;
    doc["rssi"] = WiFi.RSSI();
    
    JsonObject halObj = doc["hal"].to<JsonObject>();
    HAL::populateStatusJson(halObj);

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
