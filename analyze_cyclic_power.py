#!/usr/bin/env python3
import json
import urllib.request
import math
import statistics
from datetime import datetime

# --- Technical Utility Functions ---

def fetch_data(url):
    print(f"Fetching data from {url}...")
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            data = json.loads(response.read().decode())
            return data[0]['data']
    except Exception as e:
        print(f"Error fetching data: {e}")
        return None

def resample(data, interval_ms=60000):
    """Resample unevenly spaced data to a fixed interval using linear interpolation."""
    if not data: return []
    
    start_ts = data[0][0]
    end_ts = data[-1][0]
    
    ts_interp = []
    vals_interp = []
    
    current_ts = start_ts
    idx = 0
    
    while current_ts <= end_ts:
        # Find the segment
        while idx < len(data) - 1 and data[idx+1][0] < current_ts:
            idx += 1
            
        if idx >= len(data) - 1:
            break
            
        # Linear interpolation
        t1, v1 = data[idx]
        t2, v2 = data[idx+1]
        
        if t1 == t2:
            v = v1
        else:
            v = v1 + (v2 - v1) * (current_ts - t1) / (t2 - t1)
            
        ts_interp.append(current_ts)
        vals_interp.append(v)
        current_ts += interval_ms
        
    return ts_interp, vals_interp

# --- Option 1: FFT (Spectral Analysis) ---

def fft(x):
    """Pure Python Recursive FFT (Cooley-Tukey)."""
    N = len(x)
    if N <= 1: return x
    even = fft(x[0::2])
    odd = fft(x[1::2])
    T = [math.e**(-2j * math.pi * k / N) * odd[k] for k in range(N // 2)]
    return [even[k] + T[k] for k in range(N // 2)] + \
           [even[k] - T[k] for k in range(N // 2)]

def run_fft_analysis(vals, interval_min):
    print("\n--- Option 1: FFT (Spectral Analysis) ---")
    
    # Pad to power of 2
    N = len(vals)
    n_padded = 1 << (N - 1).bit_length()
    vals_padded = vals + [0] * (n_padded - N)
    
    # Remove DC component (mean)
    mean_val = sum(vals) / len(vals)
    vals_detrended = [v - mean_val for v in vals_padded]
    
    # Run FFT
    freq_domain = fft(vals_detrended)
    
    # Calculate magnitudes
    magnitudes = [abs(f) for f in freq_domain[:n_padded // 2]]
    
    # Identify top peaks (ignoring very low frequencies/DC)
    # Skip indices 0 to 2 (very long trends)
    peaks = []
    for i in range(3, len(magnitudes) - 1):
        if magnitudes[i] > magnitudes[i-1] and magnitudes[i] > magnitudes[i+1]:
            # freq in cycles per interval
            # period = total_intervals / i
            period_min = (n_padded * interval_min) / i
            peaks.append((period_min, magnitudes[i]))
            
    peaks.sort(key=lambda x: x[1], reverse=True)
    
    print(f"{'Period (min)':<15} | {'Magnitude':<15}")
    print("-" * 33)
    for p, m in peaks[:5]:
        print(f"{p:<15.2f} | {m:<15.2f}")

# --- Option 2: State Inference (Clustering/Duty Cycle) ---

def run_state_inference(vals):
    # Blind Discovery of Periodic Loads via Transition Clustering
    deltas = [vals[i] - vals[i-1] for i in range(1, len(vals))]
    # Filter for significant 'On' transitions (positive jumps)
    pos_jumps = [d for d in deltas if d > 50]
    
    if not pos_jumps:
        print("No significant transitions detected.")
        return

    # Cluster the magnitudes of 'On' jumps to find distinct device types
    # Simple 1D clustering on jump sizes
    j_min, j_max = min(pos_jumps), max(pos_jumps)
    k_discovery = 5
    centroids = [j_min + (j_max - j_min) * i / (k_discovery - 1) for i in range(k_discovery)]
    
    for _ in range(20):
        groups = [[] for _ in range(k_discovery)]
        for j in pos_jumps:
            idx = min(range(k_discovery), key=lambda i: abs(j - centroids[i]))
            groups[idx].append(j)
        for i in range(k_discovery):
            if groups[i]: centroids[i] = sum(groups[i]) / len(groups[i])
    
    centroids.sort()
    
    print("\n--- Blind Load Discovery (Periodic Transition Analysis) ---")
    print(f"{'Appliance ID':<15} | {'Mean Jump (W)':<15} | {'Count':<8} | {'Median Period (min)':<20}")
    print("-" * 65)
    
    for i, c_watt in enumerate(centroids):
        # Identify indices where this specific 'type' of load turned on
        indices = [idx for idx, d in enumerate(deltas) if abs(d - c_watt) < (c_watt * 0.15 + 25)]
        
        if len(indices) < 3: continue # Not enough data for period analysis
        
        # Calculate intervals between 'On' events
        intervals = [indices[j] - indices[j-1] for j in range(1, len(indices))]
        median_p = statistics.median(intervals)
        
        # Calculate consistency (Standard Deviation of intervals / Mean)
        # Low coefficient of variation means high periodicity
        if len(intervals) >= 2:
            mean_p = sum(intervals) / len(intervals)
            stdev_p = statistics.stdev(intervals)
            cv = stdev_p / mean_p if mean_p > 0 else 1.0
        else:
            cv = 1.0

        periodicity = "High" if cv < 0.2 else "Medium" if cv < 0.5 else "Low"
        
        print(f"Load Group {i:<4} | {c_watt:15.2f} | {len(indices):<8} | {median_p:5.1f} min ({periodicity} periodicity)")

# --- Option 3: Autocorrelation (ACF) ---

def run_acf_analysis(vals, interval_min):
    print("\n--- Option 3: Autocorrelation (ACF) ---")
    
    N = len(vals)
    mean = sum(vals) / N
    vals_norm = [v - mean for v in vals]
    denom = sum(v*v for v in vals_norm)
    
    if denom == 0:
        print("Empty or constant signal.")
        return

    # Check lags up to half the data length or 8 hours
    max_lag = min(N // 2, int(480 / interval_min)) 
    
    lags = []
    for lag in range(1, max_lag):
        num = sum(vals_norm[i] * vals_norm[i+lag] for i in range(N - lag))
        r = num / denom
        lags.append(r)
        
    # Find peaks in ACF
    acf_peaks = []
    for i in range(1, len(lags) - 1):
        if lags[i] > lags[i-1] and lags[i] > lags[i+1] and lags[i] > 0.1:
            acf_peaks.append((i + 1, lags[i]))
            
    acf_peaks.sort(key=lambda x: x[1], reverse=True)
    
    print(f"{'Lag (min)':<15} | {'Correlation (r)':<15}")
    print("-" * 33)
    for lag_idx, r in acf_peaks[:5]:
        print(f"{lag_idx * interval_min:<15.2f} | {r:<15.4f}")

# --- Main Execution ---

def main():
    url = "http://falcon/data/power-W/48"
    raw_data = fetch_data(url)
    
    if not raw_data:
        return
        
    print(f"Retrieved {len(raw_data)} points.")
    
    # Resample to 1-minute intervals for stability and speed
    interval_min = 1.0
    ts, vals = resample(raw_data, interval_ms=60000)
    print(f"Resampled to {len(vals)} points at {interval_min} min intervals.")
    
    run_fft_analysis(vals, interval_min)
    run_state_inference(vals)
    run_acf_analysis(vals, interval_min)

if __name__ == "__main__":
    main()
