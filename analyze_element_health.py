#!/usr/bin/env python3
import json
import urllib.request
import statistics
import math
from datetime import datetime

# --- Technical Config ---
V_NOMINAL = 240  # Standard US split-phase voltage for Water Heaters

def fetch_data(url):
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            data = json.loads(response.read().decode())
            return data[0]['data']
    except Exception as e:
        print(f"Error fetching {url}: {e}")
        return None

def resample(data, interval_ms=60000):
    if not data: return []
    start_ts, end_ts = data[0][0], data[-1][0]
    ts_interp, vals_interp = [], []
    current_ts, idx = start_ts, 0
    while current_ts <= end_ts:
        while idx < len(data) - 1 and data[idx+1][0] < current_ts:
            idx += 1
        if idx >= len(data) - 1: break
        t1, v1 = data[idx]
        t2, v2 = data[idx+1]
        v = v1 + (v2 - v1) * (current_ts - t1) / (t2 - t1) if t1 != t2 else v1
        ts_interp.append(current_ts)
        vals_interp.append(v)
        current_ts += interval_ms
    return vals_interp

def analyze_health():
    print("--- Water Heater Element Health Diagnostics ---")
    
    # URLs
    power_url = "http://falcon/data/power-W/48"
    upper_temp_url = "http://falcon/data/temp-2809853f030000a7/48"
    lower_temp_url = "http://falcon/data/temp-2813513f03000072/48"

    power_raw = fetch_data(power_url)
    u_temp_raw = fetch_data(upper_temp_url)
    l_temp_raw = fetch_data(lower_temp_url)

    if not all([power_raw, u_temp_raw, l_temp_raw]):
        print("Data acquisition failed.")
        return

    # Resample to 1-minute resolution
    p_vals = resample(power_raw)
    u_vals = resample(u_temp_raw)
    l_vals = resample(l_temp_raw)
    
    min_len = min(len(p_vals), len(u_vals), len(l_vals))
    p_vals, u_vals, l_vals = p_vals[:min_len], u_vals[:min_len], l_vals[:min_len]

    # Detect Cycles for Elements
    # Lower (~3.8kW), Upper (~4.5kW)
    # Using thresholds based on previous discovery
    lower_threshold = (3000, 4200)
    upper_threshold = (4201, 5500)

    def extract_cycles(threshold):
        cycles = []
        is_on = False
        start_idx = 0
        for i, p in enumerate(p_vals):
            # Check for specific jump relative to baseline (simplified here to absolute for health check)
            if threshold[0] <= p - 1000 <= threshold[1]: # Subtract baseload approx
                if not is_on:
                    is_on = True
                    start_idx = i
            else:
                if is_on:
                    is_on = False
                    cycles.append((start_idx, i))
        return cycles

    l_cycles = extract_cycles(lower_threshold)
    u_cycles = extract_cycles(upper_threshold)

    def process_element(cycles, temp_vals, label):
        print(f"\nAnalyzing {label}:")
        efficiencies = []
        resistances = []
        
        for start, end in cycles:
            duration_min = end - start
            if duration_min < 3: continue # Filter noise
            
            # Energy in Wh: (Sum of W) / 60
            energy_wh = sum(p_vals[start:end]) / 60.0
            
            # Thermal Rise
            # We look for the peak temperature slightly after the cycle ends due to lag
            t_start = temp_vals[start]
            t_end_idx = min(end + 10, len(temp_vals) - 1)
            t_peak = max(temp_vals[end:t_end_idx+1])
            delta_t = t_peak - t_start
            
            # Efficiency: F per Wh
            eff = delta_t / energy_wh if energy_wh > 0 else 0
            efficiencies.append(eff)
            
            # Resistance: R = V^2 / P
            avg_p_net = (sum(p_vals[start:end]) / duration_min) - 1000 # Subtract baseline
            if avg_p_net > 0:
                res = (V_NOMINAL**2) / avg_p_net
                resistances.append(res)

        if not efficiencies:
            print("  No stable cycles detected.")
            return None

        avg_eff = statistics.mean(efficiencies)
        avg_res = statistics.mean(resistances)
        print(f"  Count:          {len(efficiencies)}")
        print(f"  Avg Resistance: {avg_res:.2f} Ohms (Implied @ {V_NOMINAL}V)")
        print(f"  Avg Efficiency: {avg_eff:.6f} °F/Wh")
        return {"eff": avg_eff, "res": avg_res}

    l_metrics = process_element(l_cycles, l_vals, "Lower Element (~3.8kW)")
    u_metrics = process_element(u_cycles, u_vals, "Upper Element (~4.5kW)")

    if l_metrics and u_metrics:
        print("\n--- Comparative Analysis ---")
        res_diff = l_metrics['res'] - u_metrics['res']
        eff_ratio = l_metrics['eff'] / u_metrics['eff']
        
        print(f"Resistance Delta: {res_diff:+.2f} Ohms")
        print(f"Efficiency Ratio: {eff_ratio:.2%} (Lower vs Upper)")
        
        print("\nInterpretations:")
        if res_diff > 1.5:
            print("[!] Resistance Mismatch: Lower element has significantly higher impedance.")
            print("    Likely cause: Severe mineral scaling causing localized overheating (positive temp coeff).")
        
        if eff_ratio < 0.85:
            print("[!] Thermal Coupling Issue: Lower element is less effective at heating its sensor.")
            print("    Likely cause: Heavy sediment build-up at tank bottom insulating the element.")
        elif eff_ratio > 1.15:
            print("[?] Anomalous Efficiency: Lower element appears 'more' efficient (check sensor proximity).")
        else:
            print("[*] Uniform Efficiency: Heat transfer to sensors is consistent across both elements.")

if __name__ == "__main__":
    analyze_health()
