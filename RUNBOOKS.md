# Runbooks

## Compilation and Deployment

### Target Hardware
- **ESP-12F** (Generic ESP8266)
- **Settings**: 4MB Flash (2MB FS), 80MHz CPU, NodeMCU Reset

### Compiling hem_hvac
To compile the `hem_hvac` sketch:
```bash
./tools/arduino-cli compile --fqbn esp8266:esp8266:generic:xtal=80,eesz=4M2M,ResetMethod=nodemcu sketches/hem_hvac/hem_hvac.ino --output-dir sketches/hem_hvac/build
```

### Deployment (OTA)
To deploy via OTA to the furnace device (192.168.1.122):
```bash
python3 /home/agent/.arduino15/packages/esp8266/hardware/esp8266/3.1.2/tools/espota.py -i 192.168.1.122 -p 8266 -f sketches/hem_hvac/build/hem_hvac.ino.bin
```

## Logs and Monitoring
- Temperature Data (Falcon): `http://falcon/data/temp-tempF/01`
- Memory and Rationale: See `MEMORIES.md`
