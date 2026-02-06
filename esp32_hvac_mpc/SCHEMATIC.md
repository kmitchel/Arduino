# ESP32 HVAC Wiring Schematic

## Power Supply
```
                    ┌──────────────────┐
  AC Adapter ───────┤ 5V Power Supply  │
  (5V 2A)           │                  │
                    │  5V ────┬────────┼─── Relay Module VCC
                    │         │        │
                    │         └────────┼─── ESP32 VIN (or USB)
                    │                  │
                    │  GND ────┬───────┼─── Relay Module GND
                    │          │       │
                    │          └───────┼─── ESP32 GND
                    └──────────────────┘
```

## ESP32 to Relay Module
```
    ESP32 DevKit                      8-Channel Relay Module
    ┌────────────┐                    ┌─────────────────────┐
    │            │                    │                     │
    │      GPIO25├────────────────────┤IN1  (HEAT)          │
    │      GPIO26├────────────────────┤IN2  (COOL)          │
    │      GPIO27├────────────────────┤IN3  (FAN)           │
    │      GPIO32├────────────────────┤IN4  (HEAT_OVERRIDE) │
    │      GPIO33├────────────────────┤IN5  (FAN_OVERRIDE)  │
    │      GPIO14├────────────────────┤IN6  (COOL_OVERRIDE) │
    │            │                    │IN7  (spare)         │
    │            │                    │IN8  (spare)         │
    │            │                    │                     │
    │        GND ├────────────────────┤GND                  │
    │            │                    │                     │
    │            │   5V from PSU ─────┤VCC / JD-VCC         │
    │            │                    │                     │
    └────────────┘                    └─────────────────────┘

    Note: Relay inputs are ACTIVE LOW (GPIO LOW = relay ON)
    ESP32 3.3V logic is compatible with relay optocouplers
```

## DS18B20 Temperature Sensor
```
    ESP32                         DS18B20 (TO-92 package)
    ┌────────────┐                    ┌─────────┐
    │            │                    │         │
    │      3.3V  ├────┬───────────────┤ VDD (3) │
    │            │    │               │         │
    │            │  ┌─┴─┐             │         │
    │            │  │4.7K│            │         │
    │            │  │ Ω │            │         │
    │            │  └─┬─┘             │         │
    │            │    │               │         │
    │      GPIO4 ├────┴───────────────┤ DQ  (2) │
    │            │                    │         │
    │       GND  ├────────────────────┤ GND (1) │
    │            │                    │         │
    └────────────┘                    └─────────┘
    
    Pullup: 3.3V ──┬── 4.7KΩ ──┬── GPIO4
                   │           │
                   └── VDD     └── DQ

    DS18B20 Pinout (flat side facing you):
    ┌──────────────────┐
    │   ┌──────────┐   │
    │   │  DS18B20 │   │
    │   │          │   │
    │   └──────────┘   │
    │    │   │   │     │
    │   GND DQ  VDD    │
    │   (1) (2) (3)    │
    └──────────────────┘
```

## Complete Wiring Diagram
```
                           ┌─────────────────────────────────────┐
                           │         5V Power Supply             │
                           │         (5V 2A Adapter)             │
                           └─────────────┬───────────────────────┘
                                         │
                          ┌──────────────┴──────────────┐
                          │                             │
                          ▼                             ▼
    ┌─────────────────────────────┐     ┌──────────────────────────────┐
    │      ESP32 DevKit           │     │    8-Channel Relay Module    │
    │                             │     │                              │
    │  VIN ◄──────────────────────┼─────┤─► VCC                        │
    │  GND ◄──────────────────────┼─────┤─► GND                        │
    │                             │     │                              │
    │  GPIO25 ────────────────────┼─────┤─► IN1 (Heat)                 │
    │  GPIO26 ────────────────────┼─────┤─► IN2 (Cool)                 │
    │  GPIO27 ────────────────────┼─────┤─► IN3 (Fan)                  │
    │  GPIO32 ────────────────────┼─────┤─► IN4 (Heat Override)        │
    │  GPIO33 ────────────────────┼─────┤─► IN5 (Fan Override)         │
    │  GPIO14 ────────────────────┼─────┤─► IN6 (Cool Override)        │
    │                             │     │   IN7 (spare)                │
    │  GPIO4 ─────┬───────────┐   │     │   IN8 (spare)                │
    │             │           │   │     │                              │
    │  3.3V ──────┤           │   │     │  Relay Outputs:              │
    │            ┌┴┐          ▼   │     │  NO/COM/NC to HVAC           │
    │            │R│ 4.7K   ┌───┐ │     │                              │
    │            └┬┘        │ T │ │     │                              │
    │             │         │ E │ │     └──────────────────────────────┘
    │  GND ───────┴─────────┤ M │ │
    │                       │ P │ │
    │                       └───┘ │
    │                      DS18B20│
    │                             │
    │  GPIO2 ─► Onboard LED       │
    │                             │
    │  BT Antenna (built-in)      │
    │                             │
    └─────────────────────────────┘
```

## Relay Output Connections to HVAC
```
    Relay Module                    HVAC Thermostat Wires
    ┌──────────────────┐            ┌───────────────────────────┐
    │                  │            │                           │
    │  Relay 1 (Heat)  │            │                           │
    │    NO ───────────┼────────────┤─► W (Heat Call)           │
    │    COM ──────────┼────────────┤─► R (24V Hot)             │
    │    NC            │            │                           │
    │                  │            │                           │
    │  Relay 2 (Cool)  │            │                           │
    │    NO ───────────┼────────────┤─► Y (Cool Call)           │
    │    COM ──────────┼────────────┤─► R (24V Hot)             │
    │                  │            │                           │
    │  Relay 3 (Fan)   │            │                           │
    │    NO ───────────┼────────────┤─► G (Fan Call)            │
    │    COM ──────────┼────────────┤─► R (24V Hot)             │
    │                  │            │                           │
    └──────────────────┘            │   C (24V Common/Return)   │
                                    └───────────────────────────┘
```
