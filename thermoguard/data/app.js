const API_BASE = '/api';

// State
let currentData = null;
let optimisticTarget = null;
let updateTimer = null;
let lastUptime = 0;

async function fetchStatus() {
    try {
        const res = await fetch(`${API_BASE}/status`);
        if (!res.ok) throw new Error('Network response not ok');
        const data = await res.json();
        currentData = data;
        updateUI(data);
        setConnected(true);
    } catch (e) {
        console.error("Fetch failed:", e);
        setConnected(false);
    }
}

function setConnected(connected) {
    const status = document.getElementById('connStatus');
    const dot = document.getElementById('connDot');
    if (connected) {
        status.textContent = 'CONNECTED';
        dot.style.background = '#4ade80';
        dot.style.boxShadow = '0 0 10px #4ade80';
    } else {
        status.textContent = 'OFFLINE';
        dot.style.background = '#ef4444';
        dot.style.boxShadow = '0 0 10px #ef4444';
    }
}

function updateUI(data) {
    // 1. Current Temperature (Smooth update if possible, for now direct)
    const tempEl = document.getElementById('currentTemp');
    if (data.effTemp !== null) {
        tempEl.textContent = data.effTemp.toFixed(1);
    } else {
        tempEl.textContent = '--';
    }

    // 2. Target Temperature
    if (optimisticTarget === null) {
        document.getElementById('targetTemp').textContent = data.target.toFixed(1);
    }

    // 3. System State Label
    const stateEl = document.getElementById('sysState');
    const states = ['System Idle', 'Heating', 'Cooling', 'Fan Active', 'Waiting to Heat', 'Waiting to Cool'];
    stateEl.textContent = states[data.state] || 'Unknown';

    // Pulse animation for active states
    if (data.state === 1 || data.state === 2 || data.state === 3) {
        stateEl.classList.add('status-active');
    } else {
        stateEl.classList.remove('status-active');
    }

    // 4. Mode Selection & Theme
    document.body.className = `mode-${data.mode}`;

    document.querySelectorAll('.mode-item').forEach(btn => btn.classList.remove('active'));
    const modeBtnIds = ['modeOff', 'modeHeat', 'modeCool', '?', 'modeFan'];
    const activeBtnId = modeBtnIds[data.mode];
    if (activeBtnId && document.getElementById(activeBtnId)) {
        document.getElementById(activeBtnId).classList.add('active');
    }

    // 5. Info Cards
    const sourceEl = document.getElementById('tempSource');
    sourceEl.textContent = data.usingRemote ? 'Remote' : 'Local DS18B20';
    if (data.hal && !data.hal.sensorOk) {
        sourceEl.textContent = 'SENSOR ERROR';
        sourceEl.style.color = '#ef4444';
    } else {
        sourceEl.style.color = '';
    }

    // Uptime formatting
    if (data.hal && data.hal.uptime) {
        const up = data.hal.uptime;
        const h = Math.floor(up / 3600);
        const m = Math.floor((up % 3600) / 60);
        document.getElementById('uptimeValue').textContent = `${h}h ${m}m`;
    }
}

async function setMode(mode) {
    // Optimistic theme change
    document.body.className = `mode-${mode}`;
    document.querySelectorAll('.mode-item').forEach(btn => btn.classList.remove('active'));

    const modeBtnIds = ['modeOff', 'modeHeat', 'modeCool', '?', 'modeFan'];
    const btn = document.getElementById(modeBtnIds[mode]);
    if (btn) btn.classList.add('active');

    try {
        const res = await fetch(`${API_BASE}/mode`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mode: mode })
        });
        if (res.ok) fetchStatus();
    } catch (e) {
        console.error("Set mode failed:", e);
    }
}

async function adjustTemp(delta) {
    if (!currentData) return;

    let base = optimisticTarget !== null ? optimisticTarget : currentData.target;
    let newTemp = base + delta;

    // Safety Clamp
    if (newTemp < 50) newTemp = 50;
    if (newTemp > 90) newTemp = 90;

    // Feedback
    optimisticTarget = newTemp;
    document.getElementById('targetTemp').textContent = newTemp.toFixed(1);

    // Debounced Send
    if (updateTimer) clearTimeout(updateTimer);
    updateTimer = setTimeout(async () => {
        try {
            const res = await fetch(`${API_BASE}/target`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ temp: newTemp })
            });
            if (res.ok) {
                optimisticTarget = null;
                fetchStatus();
            }
        } catch (e) {
            console.error("Set target failed:", e);
        }
    }, 800);
}

// Initialize
fetchStatus();
setInterval(fetchStatus, 3000); // Background refresh
