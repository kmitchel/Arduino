# HVAC Tuning Session Memory - 2026-02-07

## Context
The system is a gas-fired furnace controlled by an ESP-12F (generic ESP8266) running custom firmware. The primary goal was to reduce temperature overshoot while maintaining equipment health and fuel efficiency.

## Technical Rationale (First Principles)

### 1. Minimum Runtime vs. Short-Cycling
- **Old Value**: 10 minutes (600,000ms)
- **New Value**: 5 minutes (300,000ms)
- **Decision**: 5 minutes is the "safe floor" for gas furnaces. It ensures the heat exchanger reaches a steady state and avoids flue gas condensation, while being short enough to allow the thermostat to cycle off before massive overshoots occur in lower-thermal-mass environments.

### 2. Hysteresis & Lag Compensation
- **Original Config**: `[-1.5F, +0.5F]` relative to `heatSet`.
- **Final Config**: `[-0.5F, +0.0F]` relative to `heatSet`.
- **Observations**:
    - **Thermal Inertia (Overshoot)**: The room peaked at ~73.4F even when the relay was cut at 73.0F. This confirmed a ~0.4F residual heat surge from the blower/exchanger.
    - **Ignition Lag (Floor)**: The room temperature dropped to ~71.9F when triggering at 72.5F (a -0.6F drop before reversal). This represents the ignition, heat-up, and distribution time.
- **Outcome**: The current `[-0.5, +0.0]` configuration yields a total room swing of ~1.5F (71.9F to 73.4F). This is considered the optimal "Goldilocks Zone" for balancing comfort and equipment longevity.

## Build & Deployment Commands
- **Target**: ESP-12F (generic esp8266)
- **FQBN**: `esp8266:esp8266:generic:xtal=80,eesz=4M2M,ResetMethod=nodemcu`
- **Output Directory**: `sketches/hem_hvac/build`
- **Upload Tool**: `espota.py`

### CLI Routine
```bash
./tools/arduino-cli compile --fqbn esp8266:esp8266:generic:xtal=80,eesz=4M2M,ResetMethod=nodemcu sketches/hem_hvac/hem_hvac.ino --output-dir sketches/hem_hvac/build
python3 /home/agent/.arduino15/packages/esp8266/hardware/esp8266/3.1.2/tools/espota.py -i 192.168.1.122 -p 8266 -f sketches/hem_hvac/build/hem_hvac.ino.bin
```

## Maintenance Notes
- **Do not shorten runtime below 5 minutes** to avoid heat exchanger corrosion.
- **Do not tighten hysteresis further**; current performance is limited by the physical minimum runtime and thermal inertia of the gas furnace.

## Infrastructure Risks & Non-HVAC Loads
### 1. Laundry Room Pipe Protection
- **Status**: Unconditioned zone, high freeze risk for water pipes.
- **Mitigation**: Persistent space heater (identified as the ~1.2kW / 23% duty cycle signature).
- **Optimization**: Potential for heat tape conversion to reduce load from 1.3kW down to <200W while maintaining safety.
- **Watchdog**: Monitor for the absence of the 1.2kW signature during sub-freezing ambient temperatures to detect heater failure.
