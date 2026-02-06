#ifndef WEB_H
#define WEB_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

class WebManager {
public:
    static void init();
    
private:
    static void _setupRoutes();
    static void _handleAPIStatus(AsyncWebServerRequest *request);
    static void _handleAPISetMode(AsyncWebServerRequest *request);
    static void _handleAPISetTarget(AsyncWebServerRequest *request);
};

#endif
