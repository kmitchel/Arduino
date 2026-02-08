import json
import statistics

def analyze_signatures():
    with open('power.json', 'r') as f:
        power_data = json.load(f)[0]['data']
    with open('temp.json', 'r') as f:
        temp_data = json.load(f)[0]['data']

    # Find move timestamp
    max_jump = 0
    move_ts = temp_data[0][0]
    for i in range(1, len(temp_data)):
        diff = abs(temp_data[i][1] - temp_data[i-1][1])
        if diff > max_jump:
            max_jump = diff
            move_ts = temp_data[i][0]

    power_pre = [p[1] for p in power_data if p[0] < move_ts]
    power_post = [p[1] for p in power_data if p[0] >= move_ts]

    def get_stats(data, label):
        if not data: return
        low = min(data)
        high = max(data)
        avg = sum(data) / len(data)
        
        # Power buckets to see common load signatures
        buckets = {}
        for p in data:
            b = int(p / 100) * 100
            buckets[b] = buckets.get(b, 0) + 1
        
        # Sort buckets by frequency
        sorted_buckets = sorted(buckets.items(), key=lambda x: x[1], reverse=True)
        
        print(f"--- {label} ---")
        print(f"Min (Baseload): {low:.2f} W")
        print(f"Max Peak: {high:.2f} W")
        print(f"Average: {avg:.2f} W")
        print("Top Power Signatures (W: Frequency Ratio):")
        total = len(data)
        for b, count in sorted_buckets[:5]:
            print(f"  {b:4d}-{b+100:4d}W: {count/total:7.2%}")

    get_stats(power_pre, "PRE-MOVE")
    get_stats(power_post, "POST-MOVE")

if __name__ == "__main__":
    analyze_signatures()
