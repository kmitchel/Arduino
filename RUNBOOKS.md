# Runbooks

## Compilation and Deployment

### Compiling hem_hvac
To compile the `hem_hvac` sketch:
```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 sketches/hem_hvac/hem_hvac.ino
```

### Deployment (Falcon OTA)
To deploy via the Falcon OTA workflow:
```bash
# Workflow: /deploy_falcon
```
