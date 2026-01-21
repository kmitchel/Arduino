# HVAC Model Predictive Controller with Robustness Improvements

**Advanced HVAC control system using Model Predictive Control (MPC) with machine learning**

## Features

### Core Control
- **Model Predictive Control (MPC)** - Predictive heating schedule based on learned thermal behavior
- **Thermal Learning** - Learns home heat rates across 5 temperature bins (bitter <10°F to warm >55°F)
- **Dynamic Coast Temperature** - Adjusts setback temperature based on outdoor conditions
- **Sunrise/Sunset Scheduling** - Uses real solar data from Open-Meteo API
- **Direct Relay Control** - Bypasses thermostat with MQTT-based ON/OFF commands
- **Hysteresis Control** - Maintains ±0.5°F deadband around target temperature

### Robustness (Deployed Jan 21, 2026)
- **3-Tier Weather Fallback** - Open-Meteo API → Local outdoor sensor → Hardcoded constant
- **ESP Mode Verification** - Monitors and alerts if ESP exits direct control mode
- **Network-Aware Presence Detection** - Sanity checks prevent DNS failures from forcing AWAY mode
- **Persistent Learning** - Saves only learned heat rates, not transient cycle state

### Presence Detection  
- Network ping-based home/away detection
- Multi-device support with hysteresis (3 consecutive failures to mark AWAY)
- Network sanity check (ping 8.8.8.8) to avoid false AWAY on DNS failures

### Intelligence
- **Rapid Cooling Detection** - Early coast trigger when outdoor temp dropping >2°F/hr
- **Override Detection** - Honors manual thermostat changes until heating completes
- **Sensor Watchdog** - Fallback mode if sensors silent >15 minutes
- **ESP Heartbeat** - 30-second keepalive to maintain direct control mode

##  System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  HVAC MPC Controller (Node.js)                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  Weather API │  │  Presence    │  │  Thermal     │      │
│  │  (Fallback)  │  │  Detection   │  │  Learning    │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                  │                  │              │
│         └──────────┬───────┴──────────────────┘              │
│                    ▼                                         │
│            ┌───────────────┐                                 │
│            │  MPC Decision │                                 │
│            │     Engine    │                                 │
│            └───────┬───────┘                                 │
│                    │                                         │
│         ┌──────────┴──────────┐                              │
│         ▼                     ▼                              │
│  ┌─────────────┐      ┌──────────────┐                      │
│  │ Direct Heat │      │  Thermostat  │                      │
│  │   Command   │      │   Setpoint   │                      │
│  └──────┬──────┘      └──────┬───────┘                      │
└─────────┼─────────────────────┼───────────────────────────── ┘
          │                     │
          │      MQTT Broker    │
          │                     │
┌─────────┼─────────────────────┼────────────────────────────┐
│         │                     │                            │
│         ▼                     ▼                            │
│  ┌──────────────────────────────────┐                     │
│  │  ESP32 HVAC Controller           │                     │
│  │  - Direct relay control          │                     │
│  │  - Heartbeat monitoring          │                     │
│  │  - Fallback to thermostat mode   │                     │
│  └──────────────┬───────────────────┘                     │
│                 │                                          │
│                 ▼                                          │
│         Furnace Relay                                     │
└───────────────────────────────────────────────────────────┘

External Sensors:
  - Indoor temp (temp/tempF)
  - Local outdoor temp (temp/284a046d4c2001a3)
  - Open-Meteo API (weather + sunrise/sunset)
  - Presence (network ping: phones)
```

## Installation

### Prerequisites
- Node.js
- MQTT broker (mosquitto)
- ESP32 HVAC controller with hem_hvac firmware

### Setup
```bash
cd hvac-mpc
npm install
node index.js
```

### Configuration
Edit `index.js` CONFIG section:
- Set `LAT`/`LON` for your location
- Configure `PHONES` for presence detection
- Adjust `TEMP_COMFORT`, `TEMP_COAST`, `TEMP_FALLBACK`

## MQTT Topics

### Subscribed
- `hvac/state` - HVAC system state (HeatOn, HeatReady, etc.)
- `hvac/heatSet` - Thermostat setpoint (for override detection)
- `temp/tempF` - Indoor temperature sensor
- `temp/284a046d4c2001a3` - Local outdoor temperature sensor
- `hvac/control/status` - ESP control mode status
- `hvac/warning` - ESP warning messages
- `hvac/info` - ESP info messages

### Published
- `hvac/heat/cmd` - Direct heat relay command (ON/OFF)
- `hvac/heatSet` - Thermostat setpoint (fallback compatibility)
- `hvac/mpc/heartbeat` - Keepalive for ESP direct mode
- `state` - Full MPC state JSON

## Operating Modes

### BOOT
Initial startup, waiting for sensors

### RECOVERY
Pre-heating to reach comfort temperature by target time (sunrise)

### MAINTENANCE  
Maintaining comfort temperature during day (sunrise to sunset+2hr or until rapid cooling)

### COAST
Energy-saving mode when away or during night

### AWAY
Forced coast mode when no presence detected

### OVERRIDE
Manual thermostat adjustment detected, MPC paused until heating completes

### FALLBACK
Safety mode if sensors silent >15 minutes (uses fallback temperature)

## Thermal Learning

System learns heat rates across 5 temperature bins:
- **Bitter** (<10°F): Slowest heating, highest heat loss
- **Cold** (10-25°F): Moderate heat loss
- **Cool** (25-40°F): Normal heat loss
- **Mild** (40-55°F): Lower heat loss
- **Warm** (>55°F): Minimal heat loss

Saved to `thermal_brain.json` for persistence across restarts.

## Weather Fallback Strategy

**3-Tier Hierarchy:**
1. **Primary**: Open-Meteo API (provides sunrise/sunset data)
2. **Fallback**: Local outdoor sensor `temp/284a046d4c2001a3`
3. **Last Resort**: Hardcoded 25°F (cold bin assumption)

Staleness check: Warns if API data >2 hours old.

## Safety Features

- **Sensor watchdog** - Fallback if sensors silent >15 min
- **ESP mode monitoring** - Alerts if direct control lost
- **Network sanity check** - Prevents DNS failures from forcing AWAY
- **Override detection** - Honors manual adjustments
- **Heat rate interpolation** - Uses weighted average if no samples in current temp bin

## Development History

- **2025-01**: Initial MPC implementation
- **2026-01-19**: Added heartbeat monitoring and blind safety cycle (conversation 16b0f90c)
- **2026-01-20**: Fetched and analyzed for solar hysteresis (conversation cd8d28f1)
- **2026-01-21**: Deployed robustness improvements (3-tier fallback, ESP monitoring, presence guards)

## Repository Contents

- `hvac-mpc/` - Node.js MPC controller (**this improved version**)
- `sketches/` - Arduino/ESP32 firmware sketches (.ino files)
  - `hem_hvac.ino` - HVAC relay control
  - `hem_htu.ino` - Humidity/Temperature sensor
  - `hem_boiler_plate.ino` - Boilerplate template
  - `hem_pwrmtr.ino` - Power meter
  - `hem_test.ino` - Test firmware
  - `hem_wtrsft.ino` - Water softener monitoring
- `libraries/` - Arduino libraries

## License

Personal project - use at your own risk

## Credits

Enhanced with robustness improvements by Antigravity AI (Jan 2026)
- Weather API fallback hierarchy
- ESP control mode verification  
- Network-aware presence detection
- Persistent learning optimization
