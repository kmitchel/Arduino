#ifndef PINS_H
#define PINS_H

// ==========================================
// ThermoGuard Pin Definitions
// ESP32-WROOM-32
// ==========================================

// Temperature Sensors
#define PIN_DS18B20 4  // OneWire Bus

// HVAC Control Relays (Active LOW)
#define PIN_RELAY_HEAT 25
#define PIN_RELAY_COOL 26
#define PIN_RELAY_FAN  27

// Override Relays (Active LOW)
// These force the legacy furnace controller signals
#define PIN_OVERRIDE_HEAT 32
#define PIN_OVERRIDE_FAN  33
#define PIN_OVERRIDE_COOL 14

// Status Indicators
#define PIN_LED_STATUS 2  // On-board LED

// Safety / Unused (Explicitly defined to avoid floating issues if needed)
// ...

#endif
