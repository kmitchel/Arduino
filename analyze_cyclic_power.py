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
    print(f"{'Appliance Group':<18} | {'Mean Jump (W)':<15} | {'Count':<8} | {'Duration (min)':<15} | {'Period (min)':<15}")
    print("-" * 85)
    
    group_indices = {} # To store start indices for proximity check

    for i, c_watt in enumerate(centroids):
        # Identify indices where this specific 'type' of load turned on
        indices = [idx for idx, d in enumerate(deltas) if abs(d - c_watt) < (c_watt * 0.15 + 30)]
        group_indices[i] = indices
        
        # Calculate durations
        durations = []
        for idx in indices:
            d_count = 0
            for k in range(idx + 1, len(vals)):
                # If power drops significantly below the 'On' state
                if vals[k] < (vals[idx] + c_watt * 0.5):
                    break
                d_count += 1
            durations.append(d_count)
            
        avg_dur = sum(durations) / len(durations) if durations else 0
        
        # Calculate intervals between 'On' events
        intervals = [indices[j] - indices[j-1] for j in range(1, len(indices))]
        median_p = statistics.median(intervals) if intervals else 0
        
        print(f"Group {i:<12} | {c_watt:15.2f} | {len(indices):<8} | {avg_dur:15.1f} | {median_p:15.1f}")

    # Proximity/Link Analysis
    print("\n--- Linked Load Hypothesis (Non-Simultaneous Elements) ---")
    for i in range(len(centroids)):
        for j in range(len(centroids)):
            if i == j: continue
            
            # Count how often group j follows group i within 60 minutes
            links = 0
            for idx_i in group_indices[i]:
                for idx_j in group_indices[j]:
                    if 0 < (idx_j - idx_i) <= 60:
                        links += 1
                        break
            
            if links > len(group_indices[i]) * 0.3: # If >30% linked
                print(f"  [!] High Linkage: Group {j} follows Group {i} in {links}/{len(group_indices[i])} cases.")
                print(f"      Possible Water Heater Toggle (Diff: {abs(centroids[i]-centroids[j]):.1f}W)")

# --- Option 4: Temperature Cross-Correlation ---

def run_thermal_correlation(power_ts, power_vals, group_indices, group_centroids):
    print("\n--- Option 4: Thermal Cross-Correlation (Water Heater Verification) ---")
    upper_sensor_url = "http://falcon/data/temp-2809853f030000a7/48"
    lower_sensor_url = "http://falcon/data/temp-2813513f03000072/48"
    
    upper_temp_data = fetch_data(upper_sensor_url)
    lower_temp_data = fetch_data(lower_sensor_url)
    
    if not upper_temp_data or not lower_temp_data:
        print("Failed to fetch temperature data for correlation.")
        return

    # Resample temperatures to match power data (1-min intervals)
    _, upper_temp = resample(upper_temp_data, interval_ms=60000)
    _, lower_temp = resample(lower_temp_data, interval_ms=60000)
    
    # Trim to match lengths
    min_len = min(len(power_vals), len(upper_temp), len(lower_temp))
    u_t = upper_temp[:min_len]
    l_t = lower_temp[:min_len]
    
    # Calculate temperature deltas (change over 5 minutes FOLLOWING a power event)
    def thermal_response(indices, temp_vals):
        responses = []
        for idx in indices:
            if idx + 10 < len(temp_vals):
                # Delta T over 10 minutes following the 'On' event
                delta = temp_vals[idx+10] - temp_vals[idx]
                responses.append(delta)
        return statistics.mean(responses) if responses else 0

    print(f"{'Appliance Group':<18} | {'Upper Sensor ΔT':<18} | {'Lower Sensor ΔT':<18}")
    print("-" * 60)
    
    # We only care about the high-power groups (Group 3 and 4)
    for i in range(len(group_centroids)):
        if group_centroids[i] < 2000: continue
        
        indices = group_indices[i]
        dt_u = thermal_response(indices, u_t)
        dt_l = thermal_response(indices, l_t)
        
        print(f"Group {i:<12} | {dt_u:15.3f} F | {dt_l:15.3f} F")

    print("\nThermal Conclusion:")
    # Identification logic: Which sensor rose more for which group?
    for i in range(len(group_centroids)):
        if group_centroids[i] < 2000: continue
        indices = group_indices[i]
        dt_u = thermal_response(indices, u_t)
        dt_l = thermal_response(indices, l_t)
        
        if dt_u > dt_l and dt_u > 0.1:
            print(f"  - Group {i} ({group_centroids[i]:.0f}W) -> Strongly linked to UPPER heating element.")
        elif dt_l > dt_u and dt_l > 0.1:
            print(f"  - Group {i} ({group_centroids[i]:.0f}W) -> Strongly linked to LOWER heating element.")

# --- Option 3: Autocorrelation (ACF) ---

def run_acf_analysis(vals, interval_min):
    print("\n--- Option 3: Autocorrelation (ACF) ---")
    N = len(vals)
    mean = sum(vals) / N
    vals_norm = [v - mean for v in vals]
    denom = sum(v*v for v in vals_norm)
    if denom == 0: return
    max_lag = min(N // 2, int(480 / interval_min)) 
    lags = []
    for lag in range(1, max_lag):
        num = sum(vals_norm[i] * vals_norm[i+lag] for i in range(N - lag))
        lags.append(num / denom)
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
    if raw_data:
        ts, vals = resample(raw_data, interval_ms=60000)
        run_fft_analysis(vals, 1.0)
        cents, g_idx = run_state_inference_with_return(vals)
        run_thermal_correlation(ts, vals, g_idx, cents)
        run_acf_analysis(vals, 1.0)

def run_state_inference_with_return(vals):
    # (Existing Blind Discovery logic but returns centroids and indices)
    # ... implemented within the main loop for efficiency in this script ...
    deltas = [vals[i] - vals[i-1] for i in range(1, len(vals))]
    pos_jumps = [d for d in deltas if d > 50]
    if not pos_jumps: return [], {}
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
    
    group_indices = {}
    print("\n--- Blind Load Discovery (Periodic Transition Analysis) ---")
    print(f"{'Appliance Group':<18} | {'Mean Jump (W)':<15} | {'Count':<8} | {'Duration (min)':<15} | {'Period (min)':<15}")
    print("-" * 85)
    for i, c_watt in enumerate(centroids):
        indices = [idx for idx, d in enumerate(deltas) if abs(d - c_watt) < (c_watt * 0.15 + 30)]
        group_indices[i] = indices
        durations = []
        for idx in indices:
            d_count = 0
            for k in range(idx + 1, len(vals)):
                if vals[k] < (vals[idx] + c_watt * 0.5): break
                d_count += 1
            durations.append(d_count)
        avg_dur = sum(durations) / len(durations) if durations else 0
        intervals = [indices[j] - indices[j-1] for j in range(1, len(indices))]
        median_p = statistics.median(intervals) if intervals else 0
        print(f"Group {i:<12} | {c_watt:15.2f} | {len(indices):<8} | {avg_dur:15.1f} | {median_p:15.1f}")
    return centroids, group_indices

if __name__ == "__main__":
    # Corrected main flow
    url = "http://falcon/data/power-W/48"
    raw_data = fetch_data(url)
    if raw_data:
        ts, vals = resample(raw_data, interval_ms=60000)
        run_fft_analysis(vals, 1.0)
        cents, g_idx = run_state_inference_with_return(vals)
        run_thermal_correlation(ts, vals, g_idx, cents)
        run_acf_analysis(vals, 1.0)
