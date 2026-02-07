import json
import statistics

def analyze_hvac():
    with open('power.json', 'r') as f:
        power_data = json.load(f)[0]['data']
    with open('temp.json', 'r') as f:
        temp_data = json.load(f)[0]['data']

    start_ts = min(power_data[0][0], temp_data[0][0])
    
    max_jump = 0
    move_ts = temp_data[0][0]
    for i in range(1, len(temp_data)):
        diff = abs(temp_data[i][1] - temp_data[i-1][1])
        if diff > max_jump:
            max_jump = diff
            move_ts = temp_data[i][0]
    
    move_hour = (move_ts - start_ts) / 3600000.0
    
    temp_pre = [t[1] for t in temp_data if t[0] < move_ts]
    temp_post = [t[1] for t in temp_data if t[0] >= move_ts]
    
    power_pre = [p[1] for p in power_data if p[0] < move_ts]
    power_post = [p[1] for p in power_data if p[0] >= move_ts]

    avg_temp_pre = sum(temp_pre) / len(temp_pre) if temp_pre else 0
    avg_temp_post = sum(temp_post) / len(temp_post) if temp_post else 0
    
    print(f"Average Temp Pre-Move: {avg_temp_pre:.2f} F")
    print(f"Average Temp Post-Move: {avg_temp_post:.2f} F")
    print(f"Min/Max Temp Post-Move: {min(temp_post):.2f} / {max(temp_post):.2f} F")
    
    avg_power_pre = sum(power_pre) / len(power_pre) if power_pre else 0
    avg_power_post = sum(power_post) / len(power_post) if power_post else 0
    
    print(f"Average Power Pre-Move: {avg_power_pre:.2f} W")
    print(f"Average Power Post-Move: {avg_power_post:.2f} W")

if __name__ == "__main__":
    analyze_hvac()
