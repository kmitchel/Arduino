const mqtt = require("mqtt");
const crypto = require('crypto');
const axios = require('axios');
const fs = require('fs');
const path = require('path');
const ping = require('ping');

// ==========================================
// 1. CONFIGURATION
// ==========================================
const CONFIG = {
    // Location: Columbia City / Fort Wayne area
    LAT: 41.1622,
    LON: -85.4038,

    // PHONES for presence detection (HOME if ANY respond)
    PHONES: [
        { name: "Ken", host: "speeder.icr4sh.com" },
        { name: "Elisa", host: "Elisa-s-S24-FE.icr4sh.com" }
    ],

    // Temperature Settings
    TEMP_FALLBACK: 62,   // Safety: If network fails
    TEMP_COAST: 62,      // Economy: Base coast temp (overridden by dynamic function)
    TEMP_COMFORT: 63,    // Comfort: Target temp (reduced from 68 for energy savings)

    // Schedule - Fallback values (dynamic sunrise/sunset preferred)
    TARGET_HOUR: 8,      // Fallback: 8:00 AM if sunrise unavailable
    MAINT_END_HOUR: 22,  // Fallback: 10:00 PM if sunset unavailable

    // Sunrise/Sunset based scheduling
    COAST_DELAY_AFTER_SUNSET_MIN: 120,  // Start coast 2hrs after sunset (fallback)
    TEMP_DROP_RATE_THRESHOLD: 2.0,      // °F/hr drop rate to trigger coast early
    TEMP_HISTORY_SIZE: 4,               // Number of readings for rate calculation

    // Physics
    SOAK_BUFFER_MIN: 20, // Extra minutes to heat walls/furniture (reduced for mobile home)
    DEFAULT_HEAT_RATE: 6.0, // Starting guess before learning
    MQTT_URL: "mqtt://localhost",

    // Storage
    DATA_FILE: path.join(__dirname, 'thermal_brain.json'),

    // Presence Detection Hysteresis
    PRESENCE_AWAY_THRESHOLD: 3,
    PRESENCE_HOME_THRESHOLD: 1,

    // Direct Control Settings
    HEAT_ON_DELTA: 0.5,   // Heat ON when temp < target - delta
    HEAT_OFF_DELTA: 0.5,  // Heat OFF when temp > target + delta
    MPC_HEARTBEAT_INTERVAL: 30000,  // Send heartbeat every 30 seconds

    // Weather API Robustness
    WEATHER_FALLBACK_TEMP: 25,  // Assume "cold" if API unavailable
    WEATHER_MAX_AGE_MIN: 120,   // Consider weather data stale after 2 hours

    // Presence Detection Robustness
    NETWORK_SANITY_CHECK_HOST: "8.8.8.8",  // Ping to verify network is up

    // Logging
    VERBOSE: true
};

// Temperature bins for learning (outside temp ranges)
const TEMP_BINS = [
    { name: "bitter", min: -999, max: 10, label: "< 10°F" },
    { name: "cold", min: 10, max: 25, label: "10-25°F" },
    { name: "cool", min: 25, max: 40, label: "25-40°F" },
    { name: "mild", min: 40, max: 55, label: "40-55°F" },
    { name: "warm", min: 55, max: 999, label: "> 55°F" }
];

// Helper for timestamps
const ts = () => new Date().toLocaleTimeString('en-US', { hour12: false });

const log = (category, message, data = null) => {
    const prefix = `[${ts()}][${category}]`;
    if (data !== null) {
        console.log(`${prefix} ${message}`, JSON.stringify(data));
    } else {
        console.log(`${prefix} ${message}`);
    }
};

const logVerbose = (category, message, data = null) => {
    if (CONFIG.VERBOSE) log(category, message, data);
};

// ==========================================
// 2. STATE & MEMORY
// ==========================================

// The "Brain" - Learns heat rates for different outside temps
let thermalModel = {
    // Heat rates by temperature bin (degrees gained per hour)
    heatRates: {
        bitter: { rate: CONFIG.DEFAULT_HEAT_RATE, samples: 0 },
        cold: { rate: CONFIG.DEFAULT_HEAT_RATE, samples: 0 },
        cool: { rate: CONFIG.DEFAULT_HEAT_RATE, samples: 0 },
        mild: { rate: CONFIG.DEFAULT_HEAT_RATE, samples: 0 },
        warm: { rate: CONFIG.DEFAULT_HEAT_RATE, samples: 0 }
    },
    // Current cycle tracking
    cycleStartTime: null,
    cycleStartTemp: null,
    cycleOutsideTemp: null  // Track outside temp when cycle started
};

// The "Machine" - Tracks current status
const machine = {
    state: "BOOT",
    currentTemp: null,
    outsideTemp: null,
    lastPacketTime: Date.now(),
    targetSet: 0,
    lastMpcSetpoint: 0,     // Track what MPC last commanded
    isHome: true,
    presenceDetails: {},
    consecutiveAllFails: 0,
    consecutiveAnySuccess: 0,
    hvacState: null,        // Track hvac/state (Heating, Cooling, etc)
    overrideSetpoint: null, // External override setpoint
    // Sunrise/Sunset tracking
    sunrise: null,          // Today's sunrise (Date object)
    sunset: null,           // Today's sunset (Date object)
    outsideTempHistory: [], // Ring buffer of {time, temp} for rate calculation
    // Direct control tracking
    heatCommand: false,     // Current heat command (true = ON)
    espControlMode: null,   // Track ESP control mode from hvac/control/status
    // Weather tracking (API + local sensor)
    lastWeatherUpdate: null, // Timestamp of last successful weather API fetch
    localOutdoorTemp: null,  // Local outdoor sensor (temp/284a046d4c2001a3)
    networkAvailable: true   // Track if network appears to be working
};

// ==========================================
// 3. HELPER FUNCTIONS
// ==========================================

const getTempBin = (outsideTemp) => {
    for (const bin of TEMP_BINS) {
        if (outsideTemp >= bin.min && outsideTemp < bin.max) {
            return bin;
        }
    }
    return TEMP_BINS[2]; // Default to "cool" if something weird happens
};

// Dynamic coast temperature based on outside conditions
// Colder outside = higher coast temp to reduce recovery burn
const getDynamicCoastTemp = (outsideTemp) => {
    if (outsideTemp === null) return CONFIG.TEMP_COAST;
    if (outsideTemp < 10) return 60;      // Bitter: emergency setback
    if (outsideTemp < 25) return 60;      // Cold: emergency setback
    if (outsideTemp < 40) return 60;      // Cool: normal setback (6°F delta)
    return 60;                            // Mild+: deeper setback (8°F delta)
};

const getCurrentHeatRate = () => {
    if (machine.outsideTemp === null) {
        return CONFIG.DEFAULT_HEAT_RATE;
    }
    const bin = getTempBin(machine.outsideTemp);
    const binData = thermalModel.heatRates[bin.name];

    // If we have no samples for this bin, try to interpolate from neighbors
    if (binData.samples === 0) {
        // Find closest bin with data
        let totalRate = 0;
        let totalWeight = 0;
        for (const [name, data] of Object.entries(thermalModel.heatRates)) {
            if (data.samples > 0) {
                totalRate += data.rate * data.samples;
                totalWeight += data.samples;
            }
        }
        if (totalWeight > 0) {
            return totalRate / totalWeight; // Weighted average of all learned rates
        }
        return CONFIG.DEFAULT_HEAT_RATE;
    }

    return binData.rate;
};

// Track temperature history for rate-of-change calculation
const trackTempHistory = (temp) => {
    const now = Date.now();
    machine.outsideTempHistory.push({ time: now, temp });

    // Keep only last N readings
    while (machine.outsideTempHistory.length > CONFIG.TEMP_HISTORY_SIZE) {
        machine.outsideTempHistory.shift();
    }
};

// Calculate rate of temperature drop (positive = cooling)
const getTempDropRate = () => {
    const history = machine.outsideTempHistory;
    if (history.length < 2) return 0;

    const oldest = history[0];
    const newest = history[history.length - 1];
    const hoursDiff = (newest.time - oldest.time) / (1000 * 60 * 60);

    if (hoursDiff < 0.25) return 0; // Need at least 15 mins of data

    const tempDiff = oldest.temp - newest.temp;  // Positive = dropping
    return tempDiff / hoursDiff;  // °F/hr (positive = cooling)
};

// ==========================================
// 4. PERSISTENCE (Save/Load Learning)
// ==========================================

const saveBrain = () => {
    try {
        // Only save persistent learned data, not transient cycle state
        const persistentData = {
            heatRates: thermalModel.heatRates
            // Cycle state (cycleStartTime, cycleStartTemp, cycleOutsideTemp) is transient
        };
        fs.writeFileSync(CONFIG.DATA_FILE, JSON.stringify(persistentData, null, 2));
        log("Brain", "Thermal model saved to disk");
    } catch (err) {
        log("Brain", "ERROR: Failed to save brain: " + err.message);
    }
};

const loadBrain = () => {
    try {
        if (fs.existsSync(CONFIG.DATA_FILE)) {
            const data = fs.readFileSync(CONFIG.DATA_FILE);
            const saved = JSON.parse(data);

            // Merge saved data with defaults (in case we added new bins)
            if (saved.heatRates) {
                for (const [name, data] of Object.entries(saved.heatRates)) {
                    if (thermalModel.heatRates[name]) {
                        thermalModel.heatRates[name] = data;
                    }
                }
            }

            log("Brain", "Loaded from disk. Learned rates:");
            for (const bin of TEMP_BINS) {
                const data = thermalModel.heatRates[bin.name];
                log("Brain", `  ${bin.label}: ${data.rate.toFixed(2)} deg/hr (${data.samples} samples)`);
            }
        } else {
            log("Brain", "No previous brain found. Starting fresh.");
            log("Brain", `Default heat rate: ${CONFIG.DEFAULT_HEAT_RATE} deg/hr for all conditions`);
        }
    } catch (err) {
        log("Brain", "ERROR: Failed to load brain: " + err.message);
    }
};

// ==========================================
// 5. MQTT SETUP
// ==========================================
const getRandomClientId = () => "hvac_mpc_" + crypto.randomBytes(4).toString('hex');

const clientId = getRandomClientId();
log("MQTT", `Connecting to ${CONFIG.MQTT_URL} with clientId: ${clientId}`);

const client = mqtt.connect(CONFIG.MQTT_URL, {
    clientId: clientId
});

const publishState = () => {
    const currentBin = machine.outsideTemp !== null ? getTempBin(machine.outsideTemp) : null;
    const currentRate = getCurrentHeatRate();
    const tempDropRate = getTempDropRate();
    const dynamicCoast = getDynamicCoastTemp(machine.outsideTemp);

    const statePayload = {
        mode: machine.state,
        temp: machine.currentTemp,
        outside: machine.outsideTemp,
        target: machine.targetSet,
        heatRate: currentRate.toFixed(2),
        tempBin: currentBin ? currentBin.name : "unknown",
        presence: machine.isHome ? "HOME" : "AWAY",
        presenceDetails: machine.presenceDetails,
        consecutiveFails: machine.consecutiveAllFails,
        hvacState: machine.hvacState,
        override: machine.state === "OVERRIDE" ? machine.overrideSetpoint : null,
        // New fields
        sunrise: machine.sunrise ? machine.sunrise.toLocaleTimeString() : null,
        sunset: machine.sunset ? machine.sunset.toLocaleTimeString() : null,
        tempDropRate: tempDropRate.toFixed(2),
        dynamicCoast: dynamicCoast,
        heatCommand: machine.heatCommand,
        espControlMode: machine.espControlMode
    };
    client.publish("state", JSON.stringify(statePayload));
    client.publish("hvac/presence", machine.isHome ? "HOME" : "AWAY", { retain: true });
    client.publish("hvac/furnace", machine.heatCommand ? "ON" : "OFF", { retain: true });
    logVerbose("MQTT", "Published state", statePayload);
};

// Send heartbeat to ESP to keep direct control mode alive
const sendMpcHeartbeat = () => {
    client.publish("hvac/mpc/heartbeat", Date.now().toString());
    logVerbose("Heartbeat", "Sent MPC heartbeat to ESP");
};

// Direct heat relay control
const setHeatRelay = (on) => {
    // Warn if ESP not confirmed to be in direct control mode
    if (machine.espControlMode && machine.espControlMode !== 'DIRECT') {
        log("Control", `⚠️  WARNING: ESP in ${machine.espControlMode} mode, not DIRECT. Command may be ignored.`);
    }

    if (machine.heatCommand !== on) {
        const cmd = on ? "ON" : "OFF";
        client.publish("hvac/heat/cmd", cmd);
        log("Control", `>>> DIRECT HEAT COMMAND: ${cmd} (was ${machine.heatCommand ? "ON" : "OFF"})`);
        machine.heatCommand = on;
    }
};

// Legacy thermostat control (for fallback compatibility)
const setThermostat = (temp) => {
    if (machine.targetSet !== temp) {
        client.publish("hvac/heatSet", temp.toString());
        log("Control", `>>> SETPOINT CHANGED: ${machine.targetSet}°F -> ${temp}°F (State: ${machine.state})`);
        machine.targetSet = temp;
        machine.lastMpcSetpoint = temp;  // Track what MPC commanded
    } else {
        logVerbose("Control", `Setpoint unchanged at ${temp}°F`);
    }
};

// ==========================================
// 6. CORE LOGIC (Weather, Presence, Learning)
// ==========================================

const fetchWeather = async () => {
    logVerbose("Weather", "Fetching from Open-Meteo (with sunrise/sunset)...");
    try {
        const url = `https://api.open-meteo.com/v1/forecast?` +
            `latitude=${CONFIG.LAT}&longitude=${CONFIG.LON}` +
            `&current=temperature_2m&daily=sunrise,sunset` +
            `&temperature_unit=fahrenheit&timezone=auto`;

        const response = await axios.get(url);
        if (response.data) {
            // Current temp
            if (response.data.current) {
                const oldTemp = machine.outsideTemp;
                machine.outsideTemp = response.data.current.temperature_2m;
                machine.lastWeatherUpdate = Date.now(); // Track successful fetch

                const bin = getTempBin(machine.outsideTemp);
                const dynamicCoast = getDynamicCoastTemp(machine.outsideTemp);
                log("Weather", `Outside temp: ${machine.outsideTemp}°F (${bin.label}) - Coast temp: ${dynamicCoast}°F`);

                // Track temp history for rate calculation
                trackTempHistory(machine.outsideTemp);
                const dropRate = getTempDropRate();
                if (dropRate !== 0) {
                    logVerbose("Weather", `Temp drop rate: ${dropRate.toFixed(2)}°F/hr (${machine.outsideTempHistory.length} samples)`);
                }
            }

            // Sunrise/Sunset (today)
            if (response.data.daily && response.data.daily.sunrise && response.data.daily.sunset) {
                machine.sunrise = new Date(response.data.daily.sunrise[0]);
                machine.sunset = new Date(response.data.daily.sunset[0]);
                log("Weather", `Sun: Rise=${machine.sunrise.toLocaleTimeString()}, Set=${machine.sunset.toLocaleTimeString()}`);
            }
        }
    } catch (err) {
        log("Weather", "ERROR: API failed - " + err.message + ". Using last known value: " + machine.outsideTemp);
    }
};

const checkPresence = async () => {
    logVerbose("Presence", `Pinging ${CONFIG.PHONES.length} devices...`);

    // Network sanity check: verify internet is reachable before trusting results
    let networkUp = true;
    try {
        const sanityCheck = await ping.promise.probe(CONFIG.NETWORK_SANITY_CHECK_HOST, { timeout: 2 });
        networkUp = sanityCheck.alive;
        if (!networkUp) {
            log("Presence", "⚠️  Network sanity check failed (can't reach 8.8.8.8). Skipping presence update.");
            machine.networkAvailable = false;
            return; // Don't change presence state when network is down
        }
        machine.networkAvailable = true;
    } catch (err) {
        log("Presence", `⚠️  Network sanity check error: ${err.message}. Skipping presence update.`);
        machine.networkAvailable = false;
        return;
    }

    // Ping all phones in parallel
    const results = await Promise.all(
        CONFIG.PHONES.map(async (phone) => {
            try {
                const res = await ping.promise.probe(phone.host, { timeout: 2 });
                return { name: phone.name, host: phone.host, alive: res.alive };
            } catch (err) {
                return { name: phone.name, host: phone.host, alive: false, error: err.message };
            }
        })
    );

    // Update per-phone status
    for (const result of results) {
        machine.presenceDetails[result.name] = result.alive;
    }

    // Check if ANY phone is home
    const anyoneHome = results.some(r => r.alive);
    const whoIsHome = results.filter(r => r.alive).map(r => r.name);
    const whoIsAway = results.filter(r => !r.alive).map(r => r.name);

    if (anyoneHome) {
        machine.consecutiveAllFails = 0;
        machine.consecutiveAnySuccess++;

        if (!machine.isHome && machine.consecutiveAnySuccess >= CONFIG.PRESENCE_HOME_THRESHOLD) {
            machine.isHome = true;
            log("Presence", `*** WELCOME HOME! *** ${whoIsHome.join(", ")} detected`);
        } else {
            logVerbose("Presence", `HOME: ${whoIsHome.join(", ")} | Away: ${whoIsAway.join(", ") || "none"}`);
        }
    } else {
        machine.consecutiveAnySuccess = 0;
        machine.consecutiveAllFails++;

        if (machine.isHome && machine.consecutiveAllFails >= CONFIG.PRESENCE_AWAY_THRESHOLD) {
            machine.isHome = false;
            log("Presence", `*** EVERYONE LEFT! *** No phones detected after ${machine.consecutiveAllFails} checks`);
        } else if (machine.isHome) {
            log("Presence", `All pings FAILED (${machine.consecutiveAllFails}/${CONFIG.PRESENCE_AWAY_THRESHOLD}) - still HOME`);
        } else {
            logVerbose("Presence", `All phones away. Fails: ${machine.consecutiveAllFails}`);
        }
    }
};

const updateLearning = (endTemp) => {
    if (!thermalModel.cycleStartTime || !thermalModel.cycleStartTemp) {
        logVerbose("Learning", "No active cycle to learn from");
        return;
    }

    const durationMs = Date.now() - thermalModel.cycleStartTime;
    const durationHours = durationMs / (1000 * 60 * 60);
    const outsideTempAtStart = thermalModel.cycleOutsideTemp;

    log("Learning", `Cycle ended. Duration: ${(durationHours * 60).toFixed(1)} mins`);
    log("Learning", `  Start temp: ${thermalModel.cycleStartTemp}°F, End temp: ${endTemp}°F`);
    log("Learning", `  Outside temp at start: ${outsideTempAtStart}°F`);

    if (durationHours < 0.25) {
        log("Learning", "Cycle too short (< 15 mins) - discarding");
        thermalModel.cycleStartTime = null;
        return;
    }

    if (endTemp <= thermalModel.cycleStartTemp) {
        log("Learning", "No temperature rise - discarding");
        thermalModel.cycleStartTime = null;
        return;
    }

    const tempRise = endTemp - thermalModel.cycleStartTemp;
    const measuredRate = tempRise / durationHours;

    // Determine which bin this cycle belongs to
    const bin = getTempBin(outsideTempAtStart);
    const binData = thermalModel.heatRates[bin.name];
    const oldRate = binData.rate;
    const oldSamples = binData.samples;

    // Weighted update: more samples = more weight to history
    // New samples start with 50% weight, decreasing as we get more data
    const historyWeight = Math.min(0.9, 0.5 + (oldSamples * 0.05));
    const newWeight = 1 - historyWeight;

    binData.rate = (oldRate * historyWeight) + (measuredRate * newWeight);
    binData.samples++;

    log("Learning", `*** LEARNED FOR ${bin.label} ***`);
    log("Learning", `  Measured rate: ${measuredRate.toFixed(2)} deg/hr`);
    log("Learning", `  Old rate: ${oldRate.toFixed(2)} deg/hr (${oldSamples} samples)`);
    log("Learning", `  New rate: ${binData.rate.toFixed(2)} deg/hr (${binData.samples} samples)`);
    log("Learning", `  Weight: ${(historyWeight * 100).toFixed(0)}% history, ${(newWeight * 100).toFixed(0)}% new`);

    saveBrain();

    // Reset cycle tracking
    thermalModel.cycleStartTime = null;
    thermalModel.cycleStartTemp = null;
    thermalModel.cycleOutsideTemp = null;
};

// ==========================================
// 7. MAIN CONTROL LOOP (MPC)
// ==========================================
let loopCount = 0;

const runLogic = async () => {
    loopCount++;
    const now = new Date();
    const currentHour = now.getHours();
    const currentMin = now.getMinutes();

    log("Loop", `========== TICK #${loopCount} @ ${currentHour}:${String(currentMin).padStart(2, '0')} ==========`);
    logVerbose("Loop", "Current machine state", machine);

    // 1. Inputs
    await checkPresence();

    // 2. Safety Watchdog
    const silenceMs = Date.now() - machine.lastPacketTime;
    const silenceMins = (silenceMs / 60000).toFixed(1);
    logVerbose("Safety", `Time since last sensor packet: ${silenceMins} mins`);

    if (silenceMs > 15 * 60 * 1000) {
        if (machine.state !== "FALLBACK") {
            log("Safety", `!!! ALERT: SENSORS SILENT FOR ${silenceMins} MINS - ENGAGING FALLBACK !!!`);
            machine.state = "FALLBACK";
            setHeatRelay(false);  // Turn off heat
            setThermostat(CONFIG.TEMP_FALLBACK);  // Also set thermostat for ESP fallback
        }
        publishState();
        return;
    }

    if (machine.currentTemp === null) {
        log("Loop", "Waiting for indoor temp sensor data...");
        return;
    }

    // Smart weather fallback hierarchy:
    // 1. Open-Meteo API (best - has sunrise/sunset)
    // 2. Local outdoor sensor (good - real local data)
    // 3. Hardcoded constant (last resort)
    if (machine.outsideTemp === null) {
        if (machine.localOutdoorTemp !== null) {
            log("Weather", `API unavailable. Using local outdoor sensor: ${machine.localOutdoorTemp}°F`);
            machine.outsideTemp = machine.localOutdoorTemp;
        } else {
            log("Weather", `WARNING: No weather data available. Using fallback: ${CONFIG.WEATHER_FALLBACK_TEMP}°F`);
            machine.outsideTemp = CONFIG.WEATHER_FALLBACK_TEMP;
        }
        machine.lastWeatherUpdate = Date.now(); // Prevent repeated warnings
    }

    // Check if weather data is stale (but still usable)
    if (machine.lastWeatherUpdate) {
        const weatherAge = (Date.now() - machine.lastWeatherUpdate) / (60 * 1000); // minutes
        if (weatherAge > CONFIG.WEATHER_MAX_AGE_MIN) {
            log("Weather", `⚠️  API data ${weatherAge.toFixed(0)} min old. Local sensor: ${machine.localOutdoorTemp}°F`);
        }
    }

    // 3. Manual Override - let it complete before MPC resumes
    if (machine.state === "OVERRIDE") {
        logVerbose("Override", `Manual override active at ${machine.overrideSetpoint}°F. HVAC state: ${machine.hvacState}.`);
        // We do NOT return here effectively - we want to fall through to actuation
        // But we want to skip MPC logic
    }

    // Get dynamic coast temperature based on current outside temp
    const dynamicCoast = getDynamicCoastTemp(machine.outsideTemp);

    // 4. Presence Override - maintain coast temp when away
    if (!machine.isHome) {
        if (machine.state !== "AWAY") {
            log("MPC", `User is AWAY -> Coast mode at ${dynamicCoast}°F`);
        }
        machine.state = "AWAY";

        // Maintain coast temperature with hysteresis (don't just turn off!)
        if (machine.currentTemp < dynamicCoast - CONFIG.HEAT_ON_DELTA) {
            setHeatRelay(true);
        } else if (machine.currentTemp > dynamicCoast + CONFIG.HEAT_OFF_DELTA) {
            setHeatRelay(false);
        }
        // Between thresholds: maintain current state

        setThermostat(dynamicCoast);  // Also update thermostat for ESP fallback
        publishState();
        return;
    }

    // 5. MPC Calculations - Using learned rate for current conditions
    const currentBin = getTempBin(machine.outsideTemp);
    const effectiveHeatRate = getCurrentHeatRate();
    const binData = thermalModel.heatRates[currentBin.name];

    const degreesNeeded = CONFIG.TEMP_COMFORT - machine.currentTemp;
    let hoursToHeat = degreesNeeded > 0 ? degreesNeeded / effectiveHeatRate : 0;
    let minutesToHeat = (hoursToHeat * 60) + CONFIG.SOAK_BUFFER_MIN;

    // Dynamic target time = Sunrise (with fallback)
    let targetTime;
    if (machine.sunrise) {
        targetTime = new Date(machine.sunrise);
        // If we've passed today's sunrise, use tomorrow's
        if (now > targetTime) {
            targetTime.setDate(targetTime.getDate() + 1);
        }
    } else {
        // Fallback to fixed hour
        targetTime = new Date();
        targetTime.setHours(CONFIG.TARGET_HOUR, 0, 0, 0);
        if (now > targetTime) {
            targetTime.setDate(targetTime.getDate() + 1);
        }
    }

    const triggerTime = new Date(targetTime.getTime() - (minutesToHeat * 60000));

    // Dynamic coast trigger = Sunset + offset OR rapid cooling
    let coastTriggerTime;
    if (machine.sunset) {
        coastTriggerTime = new Date(machine.sunset.getTime() + (CONFIG.COAST_DELAY_AFTER_SUNSET_MIN * 60000));
    } else {
        // Fallback to fixed hour
        coastTriggerTime = new Date();
        coastTriggerTime.setHours(CONFIG.MAINT_END_HOUR, 0, 0, 0);
    }

    const tempDropRate = getTempDropRate();
    const isRapidCooling = tempDropRate > CONFIG.TEMP_DROP_RATE_THRESHOLD;

    // Comfort window: from target (sunrise) until coast trigger (sunset+offset)
    // UNLESS rapid cooling is detected
    const isAfterTarget = now >= targetTime || (machine.sunrise && now >= machine.sunrise);
    const isBeforeCoast = now < coastTriggerTime;
    const isComfortWindow = isAfterTarget && isBeforeCoast && !isRapidCooling;

    log("MPC", `Conditions: Outside ${machine.outsideTemp}°F (${currentBin.label}), Inside ${machine.currentTemp}°F`);
    log("MPC", `Dynamic coast: ${dynamicCoast}°F (comfort: ${CONFIG.TEMP_COMFORT}°F, delta: ${CONFIG.TEMP_COMFORT - dynamicCoast}°F)`);
    log("MPC", `Sun: Target(Rise)=${targetTime.toLocaleTimeString()}, Coast=${coastTriggerTime.toLocaleTimeString()}`);
    log("MPC", `TempDrop: ${tempDropRate.toFixed(2)}°F/hr (threshold: ${CONFIG.TEMP_DROP_RATE_THRESHOLD}°F/hr, rapidCooling: ${isRapidCooling})`);
    log("MPC", `Using heat rate: ${effectiveHeatRate.toFixed(2)} deg/hr (${binData.samples} samples for this condition)`);
    log("MPC", `Calculations:`, {
        degreesNeeded: degreesNeeded.toFixed(1),
        minutesToHeat: Math.round(minutesToHeat),
        targetTime: targetTime.toLocaleTimeString(),
        triggerTime: triggerTime.toLocaleTimeString(),
        isComfortWindow: isComfortWindow,
        nowVsTrigger: now >= triggerTime ? "PAST trigger" : "BEFORE trigger"
    });

    // 6. Decision Matrix
    let nextState = "COAST";

    // Override takes precedence over everything
    if (machine.state === "OVERRIDE") {
        nextState = "OVERRIDE";
        logVerbose("MPC", "In OVERRIDE mode - skipping decision matrix");
    } else if (isComfortWindow) {
        nextState = "MAINTENANCE";
        logVerbose("MPC", `Decision: In comfort window (after sunrise, before coast, no rapid cooling) -> MAINTENANCE`);
    } else if (now >= triggerTime && now < targetTime) {
        nextState = "RECOVERY";
        logVerbose("MPC", `Decision: Past trigger time, before target -> RECOVERY`);
    } else if (isRapidCooling && isAfterTarget) {
        nextState = "COAST";
        log("MPC", `Decision: Rapid cooling detected (${tempDropRate.toFixed(2)}°F/hr) -> Early COAST`);
    } else {
        nextState = "COAST";
        logVerbose("MPC", `Decision: Outside heating window -> COAST`);
    }

    // 7. Actuation & Learning Triggers
    if (machine.state !== "RECOVERY" && nextState === "RECOVERY") {
        log("MPC", `*** STARTING LONG BURN ***`);
        log("MPC", `  Planned duration: ${Math.round(minutesToHeat)} mins`);
        log("MPC", `  Expected heat rate: ${effectiveHeatRate.toFixed(2)} deg/hr (${currentBin.label})`);
    }

    if (machine.state === "RECOVERY" && nextState !== "RECOVERY") {
        log("MPC", "*** RECOVERY COMPLETE ***");
    }

    if (machine.state !== nextState) {
        log("State", `>>> STATE CHANGE: ${machine.state} -> ${nextState}`);
    }

    machine.state = nextState;

    // 8. Direct Heat Control with Hysteresis
    let targetTemp;
    if (machine.state === "OVERRIDE") {
        targetTemp = machine.overrideSetpoint;
    } else if (machine.state === "MAINTENANCE" || machine.state === "RECOVERY") {
        targetTemp = CONFIG.TEMP_COMFORT;
    } else {
        targetTemp = dynamicCoast;
    }

    // Hysteresis control: heat ON when below target, OFF when above
    if (machine.currentTemp < targetTemp - CONFIG.HEAT_ON_DELTA) {
        setHeatRelay(true);
    } else if (machine.currentTemp > targetTemp + CONFIG.HEAT_OFF_DELTA) {
        setHeatRelay(false);
    }
    // If between the thresholds, maintain current state (hysteresis)

    // Also update thermostat setpoint for ESP fallback compatibility
    setThermostat(targetTemp);

    log("MPC", `Heat control: target=${targetTemp}°F, current=${machine.currentTemp}°F, heatCmd=${machine.heatCommand ? "ON" : "OFF"}`);

    publishState();
    log("Loop", `========== END TICK #${loopCount} ==========\n`);
};

// ==========================================
// 8. INITIALIZATION
// ==========================================
client.on("connect", () => {
    log("MQTT", "Connected to broker!");
    const topics = [
        "hvac/state",
        "hvac/heatSet",
        "temp/tempF",
        "temp/284a046d4c2001a3",
        "hvac/control/status",  // ESP control mode status
        "hvac/warning",         // ESP warnings
        "hvac/info"             // ESP info messages
    ];
    client.subscribe(topics);
    log("MQTT", "Subscribed to topics", topics);

    // Start sending heartbeats immediately
    sendMpcHeartbeat();

    fetchWeather();

    log("Init", "Running initial logic loop...");
    runLogic();
});

client.on("message", (topic, message) => {
    const raw = message.toString();
    const value = parseFloat(raw);

    logVerbose("MQTT", `Received: ${topic} = "${raw}"`);

    // Track ESP control mode
    if (topic === "hvac/control/status") {
        machine.espControlMode = raw;
        log("ESP", `Control mode: ${raw}`);
        return;
    }

    // Log ESP warnings and info
    if (topic === "hvac/warning") {
        log("ESP", `WARNING: ${raw}`);
        return;
    }
    if (topic === "hvac/info") {
        log("ESP", `INFO: ${raw}`);
        return;
    }

    // Track HVAC state for learning
    if (topic === "hvac/state") {
        const oldState = machine.hvacState;
        machine.hvacState = raw;

        // LEARNING START: Only start timer when device actually confirms it started heating
        if (raw === "HeatOn" && oldState !== "HeatOn" && (machine.state === "RECOVERY" || machine.state === "MAINTENANCE")) {
            log("Learning", `>>> DEVICE STARTED HEATING - STARTING TIMER <<<`);
            thermalModel.cycleStartTime = Date.now();
            thermalModel.cycleStartTemp = machine.currentTemp;
            thermalModel.cycleOutsideTemp = machine.outsideTemp;
        }

        // LEARNING END: Device finished cycle (went to Wait or Ready)
        if ((raw === "Wait" || raw === "HeatReady" || raw === "CoolReady") &&
            (oldState === "Heating" || oldState === "HeatOn") &&
            thermalModel.cycleStartTime) {

            log("Learning", `>>> DEVICE STOPPED HEATING - ENDING TIMER <<<`);
            updateLearning(machine.currentTemp);
        }

        // If we're in OVERRIDE and heating just stopped, resume MPC control
        if (machine.state === "OVERRIDE" && oldState === "Heating" && raw !== "Heating") {
            log("Override", `*** HEATING COMPLETE *** Resuming MPC control (was ${machine.overrideSetpoint}°F)`);
            machine.state = "BOOT";  // Will be set properly on next tick
            machine.overrideSetpoint = null;
        }
        machine.lastPacketTime = Date.now();
        return;
    }

    // Detect external heatSet changes (for override detection)
    if (topic === "hvac/heatSet" && !isNaN(value)) {
        if (raw === "?") return;

        if (machine.lastMpcSetpoint !== 0 && value !== machine.lastMpcSetpoint && machine.state !== "OVERRIDE") {
            log("Override", `*** EXTERNAL CHANGE DETECTED ***`);
            log("Override", `  MPC wanted: ${machine.lastMpcSetpoint}°F, External set: ${value}°F`);
            log("Override", `  Honoring external setpoint until heating completes`);
            machine.state = "OVERRIDE";
            machine.overrideSetpoint = value;
            machine.targetSet = value;
        }
        machine.lastPacketTime = Date.now();
        return;
    }

    if (!isNaN(value)) {
        if (topic === "temp/tempF") {
            const oldTemp = machine.currentTemp;
            machine.currentTemp = value;
            if (oldTemp !== value) {
                log("Sensor", `Indoor temp updated: ${oldTemp}°F -> ${value}°F`);
            }
        }

        // Track local outdoor temperature sensor
        if (topic === "temp/284a046d4c2001a3") {
            const oldTemp = machine.localOutdoorTemp;
            machine.localOutdoorTemp = value;
            if (oldTemp !== value) {
                logVerbose("Sensor", `Local outdoor temp updated: ${oldTemp}°F -> ${value}°F`);
            }
        }

        machine.lastPacketTime = Date.now();
    }
});

client.on("error", (err) => {
    log("MQTT", "ERROR: " + err.message);
});

client.on("offline", () => {
    log("MQTT", "WARNING: Broker connection lost!");
});

client.on("reconnect", () => {
    log("MQTT", "Attempting to reconnect...");
});

// ==========================================
// 9. STARTUP
// ==========================================
log("Init", "==============================================");
log("Init", "   HVAC MPC Controller - Direct Control Mode");
log("Init", "==============================================");
log("Init", "Configuration", {
    location: `${CONFIG.LAT}, ${CONFIG.LON}`,
    phones: CONFIG.PHONES.map(p => p.name).join(", "),
    temps: { fallback: CONFIG.TEMP_FALLBACK, baseCoast: CONFIG.TEMP_COAST, comfort: CONFIG.TEMP_COMFORT },
    schedule: { fallbackTargetHour: CONFIG.TARGET_HOUR, fallbackMaintEndHour: CONFIG.MAINT_END_HOUR },
    sunSchedule: { coastDelayAfterSunset: CONFIG.COAST_DELAY_AFTER_SUNSET_MIN + " mins" },
    tempDropThreshold: CONFIG.TEMP_DROP_RATE_THRESHOLD + "°F/hr",
    soakBuffer: CONFIG.SOAK_BUFFER_MIN + " mins",
    dynamicCoast: "Enabled (65°F bitter, 64°F cold, 62°F cool, 60°F mild+)",
    directControl: "Enabled (heartbeat every 30s)"
});

log("Init", "Temperature bins for learning:");
for (const bin of TEMP_BINS) {
    log("Init", `  ${bin.name}: ${bin.label}`);
}

loadBrain();

// Main logic loop - every 60 seconds
setInterval(runLogic, 60 * 1000);
log("Init", "Logic loop scheduled: every 60 seconds");

// MPC Heartbeat - every 30 seconds to keep ESP in direct mode
setInterval(() => {
    sendMpcHeartbeat();

    // Alert if ESP exited direct control mode
    if (machine.espControlMode && machine.espControlMode !== 'DIRECT') {
        log("ESP", `⚠️  ALERT: ESP not in direct control mode (current: ${machine.espControlMode})`);
    }
}, CONFIG.MPC_HEARTBEAT_INTERVAL);
log("Init", "MPC heartbeat scheduled: every 30 seconds");

// Weather fetch - every 30 minutes
setInterval(fetchWeather, 30 * 60 * 1000);
log("Init", "Weather fetch scheduled: every 30 minutes");

log("Init", "Startup complete. Waiting for MQTT connection...\n");
