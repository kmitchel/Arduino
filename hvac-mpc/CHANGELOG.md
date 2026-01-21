# HVAC-MPC Robustness Improvements

**Date:** January 21, 2026  
**Deployed:** falcon:/opt/state/index.js  
**Version:** 881 lines, 34.0 KB

## Changes Summary

### 1. 3-Tier Weather Fallback (CRITICAL)
**Problem:** System stalled if Open-Meteo API unavailable at startup

**Solution:**
- Primary: Open-Meteo API (has sunrise/sunset)
- Fallback: Local outdoor sensor `temp/284a046d4c2001a3`
- Last resort: Hardcoded 25°F constant

**Files:** Lines 53-60 (CONFIG), 130-133 (machine state), 350-352 (fetchWeather), 542-561 (runLogic)

### 2. ESP Direct Control Mode Verification
**Problem:** No alert if ESP exited direct control mode

**Solution:**
- Warn on every heat command if ESP not in DIRECT mode
- Periodic check every 30 seconds in heartbeat interval

**Files:** Lines 310-316 (setHeatRelay), 854-861 (heartbeat interval)

### 3. Network-Aware Presence Detection
**Problem:** DNS failures caused false AWAY state → forced coast mode

**Solution:**
- Ping 8.8.8.8 before trusting presence results
- Skip presence update if network sanity check fails

**Files:** Lines 59 (CONFIG), 383-400 (checkPresence)

### 4. Brain Save Format Cleanup
**Problem:** Saved transient cycle state that was never loaded

**Solution:**
- Only save persistent `heatRates` to thermal_brain.json
- Don't save `cycleStartTime`, `cycleStartTemp`, `cycleOutsideTemp`

**Files:** Lines 219-227 (saveBrain)

## Deployment Details

**Backup:** `/opt/state/index.js.bak.20260121_070134` (31.1 KB)  
**Current:** `/opt/state/index.js` (34.0 KB)  
**Service:** Running as user `state`, PID 205369  
**Log:** `/tmp/hvac-mpc.log`

## Verification

**All systems operational:**
- ✅ MQTT connected
- ✅ Brain loaded (4 thermal samples)
- ✅ Local outdoor sensor subscribed
- ✅ Heartbeat sending every 30s
- ✅ Presence detection working
- ✅ Weather API functional

**Local outdoor sensor active:**
```
[07:05:24][MQTT] Received: temp/284a046d4c2001a3 = "25.14"
```

## Git History
- Initial commit: MPC with thermal learning
- This commit: Robustness improvements (Jan 21, 2026)
