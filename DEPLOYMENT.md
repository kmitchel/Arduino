# Space Heater Deployment Specifications

## Hardware
- **MCU**: Bare ESP8266 (ESP-12F)
- **FQBN**: `esp8266:esp8266:generic`

## OTA Update Process
1. **Compile**: Compile the sketch to produce `hem_heater.bin`.
2. **Execute**: Run the update locally:
   ```bash
   python3 tools/espota.py -i spaceheater.local -f build/hem_heater.bin
   ```
