# HVAC Control System

Repository for managing home heating and ventilation systems, including ESP8266-based space heater controllers and central HVAC integration.

## Project Structure
- `sketches/`: Arduino/ESP8266 firmware source code.
- `hvac-mpc/`: Node.js based Model Predictive Control (MPC) system.
- `libraries/`: Required Arduino libraries.

## Deployment & Hardware Specs
For detailed instructions on board configurations (ESP-12F) and the OTA update process via `falcon`, please refer to:
- **[DEPLOYMENT.md](file:///home/kmitchel/dev/hvac-control/DEPLOYMENT.md)**

## Space Heater Controller (`hem_heater.ino`)
- Hardware: ESP-12F, DS18B20 sensor, SSR.
- Features: Multi-stage PWM, Overshoot prevention, Presence/Furnace awareness.
