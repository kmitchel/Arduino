import json
import datetime
import statistics

def analyze_time_patterns():
    with open('power.json', 'r') as f:
        power_data = json.load(f)[0]['data']
    
    # 48 hours is effectively 2 full cycles.
    # We want to see if there's a diurnal pattern.
    
    hourly_loads = {} # hour -> [samples]
    
    for ts, val in power_data:
        # Convert ms timestamp to datetime
        dt = datetime.datetime.fromtimestamp(ts / 1000.0)
        h = dt.hour
        if h not in hourly_loads:
            hourly_loads[h] = []
        hourly_loads[h].append(val)
    
    print("--- Hourly Average Power Consumption ---")
    for h in range(24):
        if h in hourly_loads:
            avg = sum(hourly_loads[h]) / len(hourly_loads[h])
            print(f"  {h:02d}:00 - {avg:7.2f} W")

if __name__ == "__main__":
    analyze_time_patterns()
