#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// NETWORK CONFIGURATION
// ==========================================
#define WIFI_SSID "Mitchell"
#define WIFI_PASSWORD "easypassword"
#define MQTT_SERVER "192.168.1.2"
#define MQTT_PORT 1883
#define HOSTNAME "hvac-esp32"
#define OTA_PASSWORD "admin"

// ==========================================
// LOCATION (for weather/sunrise)
// ==========================================
#define LOCATION_LAT 41.1622
#define LOCATION_LON -85.4038

// ==========================================
// GPIO PIN ASSIGNMENTS
// ==========================================
// Relay outputs (active LOW)
#define PIN_RELAY_HEAT 25
#define PIN_RELAY_COOL 26
#define PIN_RELAY_FAN 27
#define PIN_RELAY_HEAT_OVERRIDE 32
#define PIN_RELAY_FAN_OVERRIDE 33
#define PIN_RELAY_COOL_OVERRIDE 14

// Temperature sensor
#define PIN_DS18B20 4

// Status LED
#define PIN_STATUS_LED 2

// ==========================================
// TEMPERATURE SETTINGS
// ==========================================
#define TEMP_FALLBACK 62.0f   // Safety: If sensors fail
#define TEMP_COAST 62.0f      // Economy: Base coast (overridden by dynamic)
#define TEMP_COMFORT 68.0f    // Comfort: Target temp

// Hysteresis for direct control
#define TEMP_FILTER_ALPHA 0.2f // Smoothing factor (0.1 = heavy, 1.0 = none)
#define HEAT_ON_DELTA 0.5f    // Heat ON when temp < target - delta
#define HEAT_OFF_DELTA 0.5f   // Heat OFF when temp > target + delta

// ==========================================
// SCHEDULE (fallback if no sunrise data)
// ==========================================
#define TARGET_HOUR 8         // 8:00 AM wake time
#define MAINT_END_HOUR 22     // 10:00 PM coast start

// Sunset-based coast
#define COAST_DELAY_AFTER_SUNSET_MIN 120  // 2 hours after sunset

// Rapid cooling threshold
#define TEMP_DROP_RATE_THRESHOLD 2.0f  // °F/hr

// ==========================================
// PHYSICS / LEARNING
// ==========================================
#define SOAK_BUFFER_MIN 20    // Extra minutes for thermal mass
#define DEFAULT_HEAT_RATE 6.0f  // Starting guess (deg/hr)

// ==========================================
// TIMING INTERVALS (milliseconds)
// ==========================================
#define MPC_LOOP_INTERVAL 60000       // Main logic: 1 minute
#define WEATHER_FETCH_INTERVAL 1800000 // Weather: 30 minutes
#define MQTT_PUBLISH_INTERVAL 15000   // State publish: 15 seconds
#define BT_SCAN_INTERVAL 60000        // Bluetooth scan: 1 minute
#define TEMP_READ_INTERVAL 15000      // Temperature: 15 seconds

// ==========================================
// BLUETOOTH PRESENCE DETECTION
// ==========================================
// Phone MAC addresses to detect (lowercase, colon-separated)
#define BT_PHONE_1_NAME "Ken"
#define BT_PHONE_1_MAC "b0:d5:fb:bd:34:68"
#define BT_PHONE_1_IP "192.168.1.111"  // Static IP for fallback

#define BT_PHONE_2_NAME "Elisa"
#define BT_PHONE_2_MAC "11:22:33:44:55:66"
#define BT_PHONE_2_IP "192.168.1.102"  // Static IP for fallback

// Presence hysteresis
#define PRESENCE_AWAY_THRESHOLD 3   // Consecutive fails to mark AWAY
#define PRESENCE_HOME_THRESHOLD 1   // Successes to mark HOME

// ==========================================
// SAFETY LIMITS
// ==========================================
#define SENSOR_TIMEOUT_MS 900000     // 15 minutes sensor silence = fallback
#define MAX_HEAT_DURATION_MS 7200000 // 2 hours max continuous heat
#define MIN_HEAT_CYCLE_MS 600000     // 10 minutes minimum heat cycle
#define MIN_OFF_CYCLE_MS 300000      // 5 minutes minimum between cycles

// ==========================================
// WEB DASHBOARD
// ==========================================
#define WEB_SERVER_PORT 80
#define ENABLE_WEB_DASHBOARD true

// ==========================================
// DEBUG
// ==========================================
#define VERBOSE_LOGGING true

#endif // CONFIG_H
