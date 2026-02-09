#!/usr/bin/env python3
import json
import socket
import urllib.request
from datetime import datetime
import statistics

def fetch_data(url):
    print(f"Fetching data from {url}...")
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            data = json.loads(response.read().decode())
            return data[0]['data']
    except Exception as e:
        print(f"Error fetching data: {e}")
        return None

def find_peaks(data, window_size=20):
    """
    Find local maxima in the data.
    A point is a peak if it's the maximum in a sliding window.
    """
    peaks = []
    for i in range(window_size, len(data) - window_size):
        val = data[i][1]
        is_peak = True
        for j in range(i - window_size, i + window_size + 1):
            if data[j][1] > val:
                is_peak = False
                break
        if is_peak and val > 70: # Assuming indoor temp peaks above 70 during heating
            # Avoid duplicate peaks in the same cycle by ensuring it's the first one in the set
            if not peaks or (data[i][0] - peaks[-1][0]) > 600000: # Min 10 min between peaks
                peaks.append(data[i])
    return peaks

def main():
    # URLs from user request
    indoor_url = "http://falcon/data/temp-tempF/48"
    outdoor_url = "http://falcon/data/temp-284a046d4c2001a3/48"

    indoor_data = fetch_data(indoor_url)
    outdoor_data = fetch_data(outdoor_url)

    if not indoor_data or not outdoor_data:
        print("Failed to retrieve data.")
        return

    print(f"Retrieved {len(indoor_data)} indoor points and {len(outdoor_data)} outdoor points.")

    # Find peaks in indoor temperature (overshoot points)
    peaks = find_peaks(indoor_data)
    print(f"Detected {len(peaks)} furnace cycle peaks.")

    if len(peaks) < 2:
        print("Not enough peaks detected to analyze intervals.")
        return

    results = []
    for i in range(1, len(peaks)):
        start_ts = peaks[i-1][0]
        end_ts = peaks[i][0]
        interval_ms = end_ts - start_ts
        interval_min = interval_ms / 60000.0

        # Find outdoor temps during this interval
        interval_outdoor = [pt[1] for pt in outdoor_data if start_ts <= pt[0] <= end_ts]
        
        if interval_outdoor:
            median_outdoor = statistics.median(interval_outdoor)
            results.append({
                "interval_min": interval_min,
                "median_outdoor": median_outdoor,
                "start_time": datetime.fromtimestamp(start_ts / 1000).strftime('%Y-%m-%d %H:%M:%S')
            })

    print("\n--- Technical Summary: Cycle Intervals vs. Outdoor Temperature ---")
    print(f"{'Start Time':<20} | {'Interval (min)':<15} | {'Median Ext Temp (F)':<20}")
    print("-" * 60)
    
    intervals = []
    ext_temps = []
    
    for res in results:
        print(f"{res['start_time']:<20} | {res['interval_min']:<15.2f} | {res['median_outdoor']:<20.2f}")
        intervals.append(res['interval_min'])
        ext_temps.append(res['median_outdoor'])

    if len(results) >= 2:
        # Simple correlation calculation (Pearson)
        try:
            mean_int = sum(intervals) / len(intervals)
            mean_ext = sum(ext_temps) / len(ext_temps)
            
            num = sum((i - mean_int) * (e - mean_ext) for i, e in zip(intervals, ext_temps))
            den = (sum((i - mean_int)**2 for i in intervals) * sum((e - mean_ext)**2 for e in ext_temps))**0.5
            
            correlation = num / den if den != 0 else 0
            
            print("-" * 60)
            print(f"Average Cycle Interval: {mean_int:.2f} minutes")
            print(f"Average Outdoor Temp:   {mean_ext:.2f} F")
            print(f"Correlation (Pearson):  {correlation:.4f}")
            print("\nAnalysis:")
            if correlation < -0.5:
                print("Strong Negative Correlation: As exterior temperature drops, cycle frequency increases (shorter intervals).")
            elif correlation > 0.5:
                print("Strong Positive Correlation: (Unexpected) Cycle intervals increase as it gets colder.")
            else:
                print("Weak or No Correlation: Other factors (solar gain, internal loads) may be dominating at this timescale.")
        except Exception as e:
            print(f"\nCould not calculate correlation: {e}")

if __name__ == "__main__":
    main()
