#!/usr/bin/env python3
import math

# --- Constants & Specs ---
TANK_VOLUME_GALLONS = 50 
LBS_PER_GALLON = 8.34
SPECIFIC_HEAT_WATER = 1.0 # BTU / (lb * F)
BTU_PER_WH = 3.41214

# Standard Element Surfaces (approx)
# Standard (HWD): 10-12 inches (single fold) -> ~30-40 sq.in
# Low Watt Density (LWD): 18-24 inches (folded back) -> ~60-80 sq.in
SURFACE_AREA_HWD = 35.0  # sq.in
SURFACE_AREA_LWD = 75.0  # sq.in

def analyze_watt_density(wattage):
    print(f"\n--- Option 1: Watt-Density Analysis for {wattage}W ---")
    hwd_density = wattage / SURFACE_AREA_HWD
    lwd_density = wattage / SURFACE_AREA_LWD
    
    print(f"  HWD Density: {hwd_density:6.2f} W/sq.in")
    print(f"  LWD Density: {lwd_density:6.2f} W/sq.in")
    
    # Scaling Threshold: Above 150 W/sq.in minerals bake rapidly
    # Above 50-60 W/sq.in is considered "Low Watt Density"
    if hwd_density > 100:
        print("  [!] HWD Warning: High risk of mineral calcification (Scaling).")
    if lwd_density < 65:
        print("  [*] LWD Benefit: Low risk of 'dry firing' even with some sediment.")

def analyze_recovery(wattage):
    print(f"\n--- Option 2: Recovery Rate & Peak Demand ({wattage}W) ---")
    # T_rise = (Wh * 3.41) / (Gallons * 8.34)
    # Rate (F/hr) = (Wattage * 3.41) / (Gallons * 8.34)
    rate_f_hr = (wattage * BTU_PER_WH) / (TANK_VOLUME_GALLONS * LBS_PER_GALLON)
    
    time_to_heat_20f = 20.0 / rate_f_hr
    
    # Peak impact on Smart Meter
    pulses_per_sec = (wattage / 1000.0) / 3.6 # Assuming 1 pulse = 1 Wh (common Kh = 1.0)
    
    print(f"  Recovery Rate:   {rate_f_hr:6.2f} °F/hr")
    print(f"  Time for 20°F:   {time_to_heat_20f * 60:6.2f} min")
    print(f"  Pulse Frequency: {pulses_per_sec:6.2f} Hz (Smart Meter Telemetry Load)")

def analyze_layering(upper_w, lower_w):
    print(f"\n--- Option 3: Non-Simultaneous Load Balance ({upper_w}W Upper / {lower_w}W Lower) ---")
    
    if upper_w == lower_w:
        print("  Status: Balanced Logic. Simultaneous recovery potential is uniform.")
    elif upper_w > lower_w:
        print(f"  Status: Fast Top-Recovery. Tank priority is rapid shower recovery ({upper_w-lower_w}W bias).")
    else:
        print(f"  Status: Bulk-Heating Bias. Efficient long-term maintenance ({lower_w-upper_w}W bias).")
        
    print("  Thermal Stratification: Higher wattage at bottom causes more vigorous convection.")
    print("  Sediment Risk: Higher wattage at bottom (lower_w) increases 'popping' noise and scaling speed.")

def main():
    print("=== Water Heater Upgrade Simulation: Element Selection Diagnostics ===")
    
    current_lower = 3804 # Measured
    current_upper = 4483 # Measured
    proposed_match = 4500
    proposed_lwd = 4500
    proposed_low_w = 3500

    print("\n>>> Scenario A: KEEP CURRENT (Failing Lower Element)")
    analyze_watt_density(current_lower)
    analyze_recovery(current_lower)

    print("\n>>> Scenario B: UPGRADE TO LWD (4500W Foldback)")
    analyze_watt_density(proposed_lwd)
    analyze_recovery(proposed_lwd)

    print("\n>>> Scenario C: DOWNSIZE FOR GRID EFFICIENCY (3500W)")
    analyze_watt_density(proposed_low_w)
    analyze_recovery(proposed_low_w)
    
    analyze_layering(4500, 4500)

if __name__ == "__main__":
    main()
