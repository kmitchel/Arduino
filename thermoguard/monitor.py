import serial
import time
import sys

try:
    s = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
except Exception as e:
    print(f"Error opening serial port: {e}")
    sys.exit(1)

print("Resetting device...")
s.dtr = False
s.rts = True  # Assert RTS (Reset)
time.sleep(0.1)
s.rts = False # Release RTS
s.dtr = False

print("Monitoring output...")
finish_time = time.time() + 60
while time.time() < finish_time:
    if s.in_waiting:
        data = s.read(s.in_waiting)
        try:
            sys.stdout.write(data.decode('utf-8', errors='ignore'))
        except:
            pass
        sys.stdout.flush()
    time.sleep(0.01)
