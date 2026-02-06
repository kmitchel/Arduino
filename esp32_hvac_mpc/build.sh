#!/bin/bash
# ESP32 HVAC MPC - Build Script
# This script uses PlatformIO to build the firmware

set -e

# Find PlatformIO
PIO="${HOME}/.platformio/penv/bin/pio"

if [ ! -f "$PIO" ]; then
    echo "PlatformIO not found at $PIO"
    echo "Installing PlatformIO..."
    curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py
    python3 /tmp/get-platformio.py
fi

cd "$(dirname "$0")"

case "${1:-build}" in
    build)
        echo "Building firmware..."
        "$PIO" run
        ;;
    upload)
        echo "Uploading via serial..."
        "$PIO" run -t upload
        ;;
    ota)
        echo "Uploading via OTA to ${2:-hvac-esp32.local}..."
        "$PIO" run -t upload --upload-port "${2:-hvac-esp32.local}"
        ;;
    monitor)
        echo "Opening serial monitor..."
        "$PIO" device monitor
        ;;
    clean)
        echo "Cleaning build..."
        "$PIO" run -t clean
        rm -rf .pio
        ;;
    fs)
        echo "Uploading filesystem (SPIFFS)..."
        "$PIO" run -t uploadfs
        ;;
    *)
        echo "Usage: $0 {build|upload|ota [host]|monitor|clean|fs}"
        exit 1
        ;;
esac
