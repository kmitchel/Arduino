#ifndef CONFIG_H
#define CONFIG_H

// Branding / DNS
#define PROJECT_NAME "ThermoGuard"
#define HOSTNAME "thermoguard" 

// WiFi Credentials
#define WIFI_SSID "Mitchell"
#define WIFI_PASS "easypassword"

// OTA Security
#define OTA_PASSWORD "admin"

// Safety Settings
#define FAILSAFE_TIMEOUT_MS 300000 // 5 minutes logic timeout
#define MIN_OFF_TIME_MS     300000 // 5 minutes minimum off time (Compressor safety)
#define MAX_RUN_TIME_MS    7200000 // 2 hours max continuous run (Safety)

// Control Logic
#define HYSTERESIS_ON  0.5f
#define HYSTERESIS_OFF 0.5f
#define AUTO_DEADBAND  3.0f // Separation between Heat and Cool in Auto

// MQTT Configuration
#define MQTT_BROKER "192.168.1.2"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""
#define MQTT_TOPIC_PREFIX "thermoguard"

#endif
