#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "config.h"
#include "mpc_controller.h"
#include "relay_control.h"
#include "temp_sensor.h"
#include "presence_manager.h"

// Forward declarations
extern MPCController mpc;
extern RelayControl relays;
extern TempSensor tempSensor;
extern PresenceManager presence;
extern float outsideTemp;
extern int sunriseHour;
extern int sunriseMin;
extern int sunsetHour;
extern int sunsetMin;

class WebDashboard {
public:
    WebDashboard();
    void begin();

private:
    AsyncWebServer server;
    
    void setupRoutes();
    static String processor(const String& var);
};

// Implementation
WebDashboard::WebDashboard() : server(WEB_SERVER_PORT) {}

void WebDashboard::begin() {
    if (!ENABLE_WEB_DASHBOARD) return;
    
    setupRoutes();
    server.begin();
    Serial.println("[Web] Dashboard started on port 80");
}

void WebDashboard::setupRoutes() {
    // Serve static files
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // API: Status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["mode"] = mpc.getStateName();
        doc["temp"] = tempSensor.getTempF();
        doc["outside"] = outsideTemp;
        doc["target"] = mpc.getTargetTemp();
        doc["heatRate"] = mpc.getCurrentHeatRate();
        doc["tempBin"] = mpc.getCurrentBinLabel();
        doc["presence"] = presence.isAnyoneHome() ? "HOME" : "AWAY";
        doc["heatOn"] = mpc.shouldHeat();
        doc["dynamicCoast"] = mpc.getDynamicCoast();
        doc["sunrise"] = String(sunriseHour) + ":" + (sunriseMin < 10 ? "0" : "") + String(sunriseMin);
        doc["sunset"] = String(sunsetHour) + ":" + (sunsetMin < 10 ? "0" : "") + String(sunsetMin);
        
        // Relays
        JsonObject relayObj = doc["relays"].to<JsonObject>();
        relayObj["heat"] = relays.isHeatOn();
        relayObj["cool"] = relays.isCoolOn();
        relayObj["fan"] = relays.isFanOn();
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // API: Heat Rates (Brain)
    server.on("/api/brain", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", mpc.getHeatRatesJson());
    });
    
    // API: Presence Details
    server.on("/api/presence", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", presence.getDetailsJson());
    });
}

#endif // WEB_DASHBOARD_H
