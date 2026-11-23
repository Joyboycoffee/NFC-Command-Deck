#pragma once

const char NFC_PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>NFC Studio Pro | ESP32-C3 RC522</title>
<style>
:root {
  --bg: #030712;
  --surface: #0f172a;
  --surface-border: #1e293b;
  --primary: #38bdf8;
  --primary-hover: #0ea5e9;
  --accent: #818cf8;
  --success: #10b981;
  --danger: #ef4444;
  --warning: #f59e0b;
  --text-main: #f8fafc;
  --text-muted: #94a3b8;
}
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
body {
  background-color: var(--bg);
  color: var(--text-main);
  display: flex;
  flex-direction: column;
  align-items: center;
  min-height: 100vh;
  padding: 16px;
}
.container {
  width: 100%;
  max-width: 640px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 16px;
  background: var(--surface);
  border: 1px solid var(--surface-border);
  border-radius: 12px;
}
.logo-area { display: flex; align-items: center; gap: 10px; }
.logo-icon { font-size: 1.5rem; }
.logo-title { font-weight: 700; font-size: 1.1rem; letter-spacing: 0.5px; }
.badge {
  font-size: 0.72rem;
  font-weight: 700;
  padding: 4px 10px;
  border-radius: 9999px;
  letter-spacing: 0.5px;
  text-transform: uppercase;
}
.badge-online { background: rgba(16, 185, 129, 0.2); color: var(--success); border: 1px solid var(--success); }
.badge-armed { background: rgba(245, 158, 11, 0.2); color: var(--warning); border: 1px solid var(--warning); animation: pulse 1s infinite; }
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }

.card {
  background: var(--surface);
  border: 1px solid var(--surface-border);
  border-radius: 12px;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.card-title {
  font-size: 0.88rem;
  font-weight: 700;
  text-transform: uppercase;
  color: var(--text-muted);
  display: flex;
  align-items: center;
  gap: 6px;
}

.arm-banner {
  background: rgba(245, 158, 11, 0.15);
  border: 1px dashed var(--warning);
  border-radius: 10px;
  padding: 12px 16px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}
.arm-content { display: flex; align-items: center; gap: 10px; color: #fbbf24; font-size: 0.85rem; font-weight: 600; }
.btn-cancel {
  background: rgba(239, 68, 68, 0.2);
  border: 1px solid var(--danger);
  color: #fca5a5;
  padding: 6px 12px;
  border-radius: 6px;
  font-size: 0.75rem;
  cursor: pointer;
  font-weight: 600;
}
.btn-cancel:hover { background: rgba(239, 68, 68, 0.4); }

.tag-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}
.tag-field {
  background: #0b0f19;
  border: 1px solid var(--surface-border);
  border-radius: 8px;
  padding: 10px 12px;
}
.field-label { font-size: 0.7rem; color: var(--text-muted); text-transform: uppercase; margin-bottom: 4px; }
.field-value { font-size: 0.95rem; font-weight: 700; color: #fff; font-family: monospace; word-break: break-all; }

.tag-payload-box {
  background: #0b0f19;
  border: 1px solid var(--surface-border);
  border-radius: 8px;
  padding: 12px 14px;
}
.payload-label { font-size: 0.7rem; color: var(--text-muted); text-transform: uppercase; margin-bottom: 6px; }
.payload-value {
  font-size: 0.95rem;
  color: var(--primary);
  font-family: monospace;
  word-break: break-all;
  line-height: 1.4;
}

.open-btn {
  background: linear-gradient(135deg, #10b981 0%, #059669 100%);
  color: #fff;
  border: none;
  border-radius: 10px;
  padding: 14px 20px;
  font-size: 0.92rem;
  font-weight: 700;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  box-shadow: 0 0 20px rgba(16, 185, 129, 0.4);
  text-decoration: none;
  transition: transform 0.15s;
}
.open-btn:hover { transform: scale(1.02); opacity: 0.95; }
.open-btn:active { transform: scale(0.98); }

.popup-warn {
  padding: 10px 14px;
  border-radius: 8px;
  background: rgba(245, 158, 11, 0.15);
  border: 1px solid var(--warning);
  color: #fbbf24;
  font-size: 0.78rem;
  line-height: 1.4;
}

.toggle-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 14px;
  background: #0b0f19;
  border: 1px solid var(--surface-border);
  border-radius: 8px;
  font-size: 0.85rem;
}

.tabs {
  display: flex;
  gap: 6px;
  background: #0b0f19;
  padding: 4px;
  border-radius: 10px;
  border: 1px solid var(--surface-border);
  overflow-x: auto;
}
.tab-btn {
  flex: 1;
  padding: 8px 10px;
  background: transparent;
  border: none;
  color: var(--text-muted);
  font-size: 0.75rem;
  font-weight: 600;
  border-radius: 7px;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.2s;
}
.tab-btn.active {
  background: var(--surface);
  color: var(--primary);
  box-shadow: 0 2px 8px rgba(0,0,0,0.4);
}
.tab-content { display: none; }
.tab-content.active { display: flex; flex-direction: column; gap: 12px; }

.input-group { display: flex; flex-direction: column; gap: 6px; }
label { font-size: 0.75rem; color: var(--text-muted); font-weight: 500; }
input[type="text"], select {
  background: #0b0f19;
  border: 1px solid var(--surface-border);
  border-radius: 8px;
  padding: 10px 14px;
  color: #fff;
  font-size: 0.88rem;
  outline: none;
}
input[type="text"]:focus, select:focus { border-color: var(--primary); }

.presets { display: flex; gap: 6px; flex-wrap: wrap; }
.preset-btn {
  background: #1e293b;
  border: 1px solid #334155;
  color: #cbd5e1;
  padding: 5px 10px;
  border-radius: 6px;
  font-size: 0.72rem;
  cursor: pointer;
}
.preset-btn:hover { background: #334155; color: #fff; }

.btn {
  padding: 11px 18px;
  border-radius: 8px;
  border: none;
  font-weight: 600;
  font-size: 0.85rem;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
}
.btn-primary { background: var(--primary); color: #042f2e; }
.btn-primary:hover { background: var(--primary-hover); }
.btn-accent { background: var(--accent); color: #0f172a; }
.btn-secondary { background: #334155; color: #fff; }
.btn-secondary:hover { background: #475569; }
.btn-danger { background: rgba(239, 68, 68, 0.2); border: 1px solid var(--danger); color: #fca5a5; }
.btn-danger:hover { background: rgba(239, 68, 68, 0.4); }

.raw-dump-box {
  background: #000;
  border: 1px solid #1e293b;
  border-radius: 8px;
  padding: 10px 12px;
  font-family: monospace;
  font-size: 0.72rem;
  color: #a7f3d0;
  max-height: 120px;
  overflow-y: auto;
  white-space: pre-wrap;
  word-break: break-all;
}

.history-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
  max-height: 160px;
  overflow-y: auto;
}
.history-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  background: #0b0f19;
  border: 1px solid var(--surface-border);
  padding: 8px 12px;
  border-radius: 8px;
}
.history-uid { font-family: monospace; font-size: 0.8rem; font-weight: 700; color: #fff; }

.footer-info {
  display: flex;
  justify-content: space-between;
  font-size: 0.72rem;
  color: var(--text-muted);
  padding: 0 4px;
  flex-wrap: wrap;
  gap: 6px;
}
</style>
</head>
<body>

<div class="container">
  <!-- HEADER -->
  <div class="header">
    <div class="logo-area">
      <span class="logo-icon">⚡</span>
      <div class="logo-title">NFC STUDIO PRO</div>
    </div>
    <div id="statusBadge" class="badge badge-online">
      <span style="font-size: 8px;">●</span> ONLINE
    </div>
  </div>

  <!-- ARMED BANNER -->
  <div id="armBanner" class="arm-banner" style="display: none;">
    <div class="arm-content">
      <span>🎯</span>
      <div>
        <div>READY TO WRITE - TAP TAG NOW</div>
        <div id="armText" style="font-size: 0.75rem; color: #fef3c7; font-weight: 400;">Armed</div>
      </div>
    </div>
    <button class="btn-cancel" onclick="cancelArm()">Cancel</button>
  </div>

  <!-- LIVE SCAN INSPECTOR -->
  <div class="card">
    <div class="card-header">
      <div class="card-title"><span>📡</span> Live Tag Inspector</div>
      <div id="scanTime" style="font-size: 0.72rem; color: var(--text-muted);">Waiting for tag...</div>
    </div>

    <div class="tag-grid">
      <div class="tag-field">
        <div class="field-label">Card UID</div>
        <div id="tagUid" class="field-value">--:--:--:--</div>
      </div>
      <div class="tag-field">
        <div class="field-label">Chip Type</div>
        <div id="tagType" class="field-value">No Tag Detected</div>
      </div>
      <div class="tag-field">
        <div class="field-label">SAK / Protocol</div>
        <div id="tagSak" class="field-value">--</div>
      </div>
      <div class="tag-field">
        <div class="field-label">Total Capacity</div>
        <div id="tagSize" class="field-value">--</div>
      </div>
    </div>

    <div class="tag-payload-box">
      <div class="payload-label">Stored Payload / URL</div>
      <div id="tagPayload" class="payload-value">Place an NFC sticker or keyfob on the reader</div>
    </div>

    <!-- DIRECT BROWSER LAUNCH BUTTON -->
    <a id="btnOpenBrowser" href="#" target="_blank" class="open-btn" style="display: none; padding: 14px 18px; font-size: 0.95rem; font-weight: 700; text-align: center; border-radius: 10px; background: linear-gradient(135deg, #10b981, #059669); color: white; text-decoration: none; box-shadow: 0 4px 15px rgba(16, 185, 129, 0.4); margin-top: 8px;">
      🚀 <span>OPEN LINK: </span><span id="btnOpenUrlText" style="text-decoration: underline; word-break: break-all;"></span>
    </a>

    <!-- POPUP BLOCKED WARNING -->
    <div id="popupBlockedWarn" class="popup-warn" style="display: none; background: rgba(245, 158, 11, 0.2); border: 1px solid #f59e0b; border-radius: 8px; padding: 12px; margin-top: 10px; font-size: 0.82rem; line-height: 1.5; color: #fef3c7;">
      ⚠️ <strong>Chrome Blocked Automatic Pop-Up:</strong><br>
      • Click the <strong>Pop-up blocked [X]</strong> icon on the right side of your Chrome address bar &rarr; select <strong>"Always allow pop-ups and redirects from this site"</strong> &rarr; Done.<br>
      • <em>Alternative:</em> Switch <strong>Auto-Launch Action</strong> to <strong>"Direct Redirect"</strong> below (never blocked by browsers).<br>
      • Or click the green button above to open instantly!
    </div>

    <!-- RAW MEMORY DUMP PREVIEW -->
    <div id="rawDumpContainer" style="display: none;">
      <div class="payload-label">Raw Hex Dump Across Blocks</div>
      <div id="rawDumpText" class="raw-dump-box">--</div>
    </div>
  </div>

  <!-- AUTO-OPEN TOGGLE & MODE -->
  <div class="card" style="padding: 12px 16px; gap: 10px;">
    <div class="toggle-row">
      <div>
        <strong>🚀 Auto-Open Link on Tap</strong>
        <div style="font-size: 0.72rem; color: var(--text-muted);">Instantly trigger browser when tag or mapped card is tapped</div>
      </div>
      <input type="checkbox" id="chkAutoOpen" checked style="width: 22px; height: 22px; cursor: pointer;">
    </div>
    <div style="display: flex; align-items: center; justify-content: space-between; font-size: 0.78rem; padding: 0 4px;">
      <label for="selOpenMode" style="color: var(--text-muted);">Auto-Launch Action:</label>
      <select id="selOpenMode" style="padding: 4px 8px; font-size: 0.75rem;">
        <option value="newtab">Open in New Tab (Popup)</option>
        <option value="redirect">Direct Redirect (Current Window - Never Blocked)</option>
      </select>
    </div>
  </div>

  <!-- PROGRAMMER / WRITER TOOL -->
  <div class="card">
    <div class="card-header">
      <div class="card-title"><span>✍️</span> Tag Programmer & Tools</div>
    </div>

    <div class="tabs">
      <button class="tab-btn active" onclick="openTab('tab-url')">URL / Social</button>
      <button class="tab-btn" onclick="openTab('tab-text')">Keyfob Text</button>
      <button class="tab-btn" onclick="openTab('tab-uidmap')">UID Mapping</button>
      <button class="tab-btn" onclick="openTab('tab-tools')">Maintenance</button>
    </div>

    <!-- TAB 1: URL / PHONE TAP -->
    <div id="tab-url" class="tab-content active">
      <div class="input-group">
        <label>URL / Social Link (Universal: NFC Stickers & Mifare Cards/Keyfobs)</label>
        <input type="text" id="urlInput" placeholder="https://instagram.com/your_handle" value="https://instagram.com/">
      </div>
      <div class="presets">
        <button class="preset-btn" onclick="setPreset('https://instagram.com/')">Instagram</button>
        <button class="preset-btn" onclick="setPreset('https://youtube.com/')">YouTube</button>
        <button class="preset-btn" onclick="setPreset('https://github.com/')">GitHub</button>
        <button class="preset-btn" onclick="setPreset('tel:+91')">Phone Call</button>
      </div>
      <button class="btn btn-primary" onclick="armWrite('url')">
        <span>⚡</span> ARM WRITER (Tap Card, Keyfob, or Sticker)
      </button>
    </div>

    <!-- TAB 2: KEYFOB TEXT -->
    <div id="tab-text" class="tab-content">
      <div class="input-group">
        <label>Custom Text / Trigger String (Universal: NTAG + Mifare Classic)</label>
        <input type="text" id="textInput" placeholder="Desk_Key_01" value="Desk_Key_01">
      </div>
      <button class="btn btn-accent" onclick="armWrite('text')">
        <span>🔑</span> ARM WRITER (Tap Keyfob, Card, or Sticker)
      </button>
    </div>

    <!-- TAB 3: UID QUICK ACTION MAPPER -->
    <div id="tab-uidmap" class="tab-content">
      <div class="input-group">
        <label>Card UID to Trigger (Auto-fills with last scanned card)</label>
        <div style="display: flex; gap: 8px;">
          <input type="text" id="uidMapInput" placeholder="57:61:A8:E3" style="flex: 1;">
          <button class="preset-btn" onclick="fillLastUid()">Use Scanned</button>
        </div>
      </div>
      <div class="input-group">
        <label>URL or Action to Open When This UID is Scanned</label>
        <input type="text" id="uidUrlInput" placeholder="https://youtube.com/" value="https://youtube.com/">
      </div>
      <button class="btn btn-primary" onclick="saveUidMapping()">
        <span>💾</span> Save UID Link Trigger
      </button>
      <div id="uidMapList" style="font-size: 0.75rem; color: var(--text-muted); margin-top: 6px;"></div>
    </div>

    <!-- TAB 4: TOOLS -->
    <div id="tab-tools" class="tab-content">
      <div style="display:flex; gap:10px;">
        <button class="btn btn-secondary" style="flex:1;" onclick="armWrite('dump')">
          <span>📋</span> Dump Next Tag
        </button>
        <button class="btn btn-danger" style="flex:1;" onclick="armWrite('wipe')">
          <span>🗑️</span> Wipe Next Tag
        </button>
      </div>
    </div>
  </div>

  <!-- RECENT SCANS HISTORY -->
  <div class="card">
    <div class="card-header">
      <div class="card-title"><span>🕒</span> Recent Scans</div>
      <button class="preset-btn" onclick="clearHistory()">Clear</button>
    </div>
    <div id="historyList" class="history-list">
      <div style="color:var(--text-muted); font-size:0.75rem; text-align:center; padding:12px;">No scans recorded yet</div>
    </div>
  </div>

  <!-- FOOTER DIAGNOSTICS -->
  <div class="footer-info">
    <span id="wifiDiag">Wi-Fi: Connecting...</span>
    <span id="txDiag">TX Power: 11 dBm (RF Protected)</span>
    <span id="rc522Diag">RC522: 43dB Optimized</span>
  </div>
</div>

<script>
let lastUid = "";
let lastScanId = 0;
let historyArr = [];
let uidMappings = {};
let audioCtx = null;
let isPolling = false;

function escapeHtml(str) {
  if (!str) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

try {
  uidMappings = JSON.parse(localStorage.getItem('nfc_uid_mappings') || '{}');
} catch(e) { uidMappings = {}; }

function renderUidMappings() {
  const container = document.getElementById('uidMapList');
  const keys = Object.keys(uidMappings);
  if (keys.length === 0) {
    container.innerHTML = '<em>No UID triggers saved yet. Map any card UID above to auto-launch any custom link!</em>';
    return;
  }
  let html = '<strong>Active Mappings:</strong><br>';
  keys.forEach(uid => {
    html += `<div style="margin-top:4px; display:flex; justify-content:space-between; align-items:center;">
      <span><code style="color:#38bdf8;">${uid}</code> &rarr; ${uidMappings[uid]}</span>
      <button class="preset-btn" style="padding:2px 6px; font-size:0.68rem;" onclick="deleteUidMapping('${uid}')">&times;</button>
    </div>`;
  });
  container.innerHTML = html;
}
renderUidMappings();

function getMappedUrl(uid) {
  if (!uid || uid === '--:--:--:--') return null;
  const clean = uid.toUpperCase().replace(/[^0-9A-F]/g, '');
  for (let k in uidMappings) {
    if (k.toUpperCase().replace(/[^0-9A-F]/g, '') === clean) {
      return uidMappings[k];
    }
  }
  return null;
}

function saveUidMapping() {
  let uid = document.getElementById('uidMapInput').value.trim().toUpperCase();
  let url = document.getElementById('uidUrlInput').value.trim();
  if (!uid || !url) return alert('Please enter both UID and URL!');
  if (!url.startsWith('http://') && !url.startsWith('https://')) {
    url = 'https://' + url;
  }
  const cleanHex = uid.replace(/[^0-9A-F]/g, '');
  if (cleanHex.length % 2 === 0 && cleanHex.length >= 8 && !uid.includes(':')) {
    uid = cleanHex.match(/.{1,2}/g).join(':');
  }
  uidMappings[uid] = url;
  localStorage.setItem('nfc_uid_mappings', JSON.stringify(uidMappings));
  renderUidMappings();
  alert(`Mapped UID ${uid} to ${url}!`);
}

function deleteUidMapping(uid) {
  delete uidMappings[uid];
  localStorage.setItem('nfc_uid_mappings', JSON.stringify(uidMappings));
  renderUidMappings();
}

function fillLastUid() {
  if (lastUid && lastUid !== '--:--:--:--') {
    document.getElementById('uidMapInput').value = lastUid;
  } else {
    alert('No tag scanned yet!');
  }
}

function playBeep() {
  try {
    if (!audioCtx) {
      audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    }
    if (audioCtx.state === 'suspended') {
      audioCtx.resume();
    }
    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.type = "sine";
    osc.frequency.setValueAtTime(880, audioCtx.currentTime);
    gain.gain.setValueAtTime(0.1, audioCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.15);
    osc.connect(gain);
    gain.connect(audioCtx.destination);
    osc.start();
    osc.stop(audioCtx.currentTime + 0.15);
  } catch(e) {}
}

function openTab(tabId) {
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
  event.target.classList.add('active');
  document.getElementById(tabId).classList.add('active');
}

function setPreset(val) {
  document.getElementById('urlInput').value = val;
  document.getElementById('urlInput').focus();
}

async function armWrite(type) {
  let payload = "";
  if (type === 'url') {
    payload = document.getElementById('urlInput').value.trim();
    if (!payload) return alert("Please enter a valid URL!");
  } else if (type === 'text') {
    payload = document.getElementById('textInput').value.trim();
    if (!payload) return alert("Please enter text!");
  }

  try {
    const res = await fetch('/api/arm', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: `type=${encodeURIComponent(type)}&payload=${encodeURIComponent(payload)}`
    });
    const data = await res.json();
    if (data.ok) {
      updateBanner(type, payload);
    }
  } catch (err) {
    alert("Connection error: " + err);
  }
}

async function cancelArm() {
  await fetch('/api/cancel', { method: 'POST' });
  document.getElementById('armBanner').style.display = 'none';
  document.getElementById('statusBadge').className = 'badge badge-online';
  document.getElementById('statusBadge').innerHTML = '<span style="font-size: 8px;">●</span> ONLINE';
}

function updateBanner(type, payload) {
  document.getElementById('armBanner').style.display = 'flex';
  document.getElementById('armText').textContent = type.toUpperCase() + (payload ? ` ("${payload}")` : "");
  document.getElementById('statusBadge').className = 'badge badge-armed';
  document.getElementById('statusBadge').innerHTML = '<span style="font-size: 8px;">●</span> ARMED';
}

function clearHistory() {
  historyArr = [];
  renderHistory();
}

function renderHistory() {
  const container = document.getElementById('historyList');
  if (historyArr.length === 0) {
    container.innerHTML = '<div style="color:var(--text-muted); font-size:0.75rem; text-align:center; padding:12px;">No scans recorded yet</div>';
    return;
  }
  container.innerHTML = historyArr.map(item => `
    <div class="history-item">
      <div>
        <div class="history-uid">${escapeHtml(item.uid)}</div>
        <div style="font-size:0.68rem; color:var(--text-muted);">${escapeHtml(item.type)} • ${escapeHtml(item.time)}</div>
      </div>
      <div style="font-size:0.75rem; color:#38bdf8; max-width:260px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap;">
        ${escapeHtml(item.payload)}
      </div>
    </div>
  `).join('');
}

async function pollStatus() {
  if (isPolling) return;
  isPolling = true;
  try {
    const res = await fetch('/api/status');
    const data = await res.json();

    document.getElementById('wifiDiag').textContent = `Wi-Fi: ${data.wifi_ssid} (${data.wifi_rssi} dBm)`;
    document.getElementById('txDiag').textContent = `TX Power: ${data.tx_power} (RF Protected)`;
    document.getElementById('rc522Diag').textContent = `RC522: ${data.rc522_version} (48dB Max Range)`;

    if (data.armed) {
      updateBanner(data.armed_type, data.armed_payload);
    } else {
      document.getElementById('armBanner').style.display = 'none';
      document.getElementById('statusBadge').className = 'badge badge-online';
      document.getElementById('statusBadge').innerHTML = '<span style="font-size: 8px;">●</span> ONLINE';
    }

    const scanTimeElem = document.getElementById('scanTime');
    if (data.tag && data.tag.card_present) {
      scanTimeElem.innerHTML = '<span style="color:#10b981; font-weight:600;">● CARD ON SENSOR</span>';
    } else if (lastScanId > 0) {
      scanTimeElem.innerHTML = '<span style="color:#94a3b8;">○ Card Lifted (Scan Saved)</span>';
    } else {
      scanTimeElem.innerHTML = '<span style="color:#94a3b8;">Waiting for tag...</span>';
    }

    if (data.tag && data.tag.uid && data.tag.uid !== "--:--:--:--") {
      document.getElementById('tagUid').textContent = data.tag.uid;
      document.getElementById('tagType').textContent = data.tag.type;
      document.getElementById('tagSak').textContent = 'SAK: 0x' + data.tag.sak;
      document.getElementById('tagSize').textContent = data.tag.size + ' Bytes';

      let pText = data.tag.payload || "";
      let targetUrl = "";

      if (pText.startsWith("http://") || pText.startsWith("https://")) {
        targetUrl = pText;
      } else {
        let mapped = getMappedUrl(data.tag.uid);
        if (mapped) {
          targetUrl = mapped;
          pText = `[Mapped UID Action] &rarr; ${targetUrl}`;
        }
      }

      if (targetUrl) {
        if (!targetUrl.startsWith("http://") && !targetUrl.startsWith("https://")) {
          targetUrl = "https://" + targetUrl;
        }
        document.getElementById('tagPayload').innerHTML = `<a href="${encodeURI(targetUrl)}" target="_blank" style="color:#38bdf8; text-decoration:underline; font-weight:600;">${escapeHtml(pText)}</a>`;
        document.getElementById('btnOpenBrowser').style.display = 'flex';
        document.getElementById('btnOpenBrowser').href = targetUrl;
        document.getElementById('btnOpenUrlText').textContent = targetUrl;
      } else {
        document.getElementById('tagPayload').textContent = pText || "(Blank / Unformatted Memory)";
        document.getElementById('btnOpenBrowser').style.display = 'none';
        document.getElementById('popupBlockedWarn').style.display = 'none';
      }

      if (data.tag.raw_hex && data.tag.raw_hex.length > 0) {
        document.getElementById('rawDumpContainer').style.display = 'block';
        document.getElementById('rawDumpText').textContent = data.tag.raw_hex;
      } else {
        document.getElementById('rawDumpContainer').style.display = 'none';
      }

      // STRICT NEW TAP: Only fires ONCE per physical tap when scan_id changes
      let isNewTap = data.tag.scan_id && (data.tag.scan_id !== lastScanId);
      if (isNewTap) {
        lastScanId = data.tag.scan_id;
        lastUid = data.tag.uid;
        playBeep();

        // AUTO-OPEN CHECK
        if (targetUrl && document.getElementById('chkAutoOpen').checked) {
          const mode = document.getElementById('selOpenMode').value;
          if (mode === 'redirect') {
            window.location.href = targetUrl;
          } else {
            try {
              const popWin = window.open(targetUrl, '_blank');
              if (!popWin || popWin.closed || typeof popWin.closed == 'undefined') {
                document.getElementById('popupBlockedWarn').style.display = 'block';
              } else {
                document.getElementById('popupBlockedWarn').style.display = 'none';
              }
            } catch(e) {
              document.getElementById('popupBlockedWarn').style.display = 'block';
            }
          }
        }

        historyArr.unshift({
          uid: data.tag.uid,
          type: data.tag.type,
          payload: targetUrl || pText || "Scan",
          time: new Date().toLocaleTimeString()
        });
        if (historyArr.length > 15) historyArr.pop();
        renderHistory();
      }
    }
  } catch (e) {
    console.error("pollStatus error:", e);
  } finally {
    isPolling = false;
    setTimeout(pollStatus, 400); // Adaptive clean polling
  }
}

// Restore settings from localStorage
const savedAutoOpen = localStorage.getItem('nfc_auto_open');
if (savedAutoOpen !== null) {
  document.getElementById('chkAutoOpen').checked = (savedAutoOpen === 'true');
}
const savedOpenMode = localStorage.getItem('nfc_open_mode');
if (savedOpenMode) {
  document.getElementById('selOpenMode').value = savedOpenMode;
}
document.getElementById('chkAutoOpen').addEventListener('change', (e) => {
  localStorage.setItem('nfc_auto_open', e.target.checked);
});
document.getElementById('selOpenMode').addEventListener('change', (e) => {
  localStorage.setItem('nfc_open_mode', e.target.value);
});

pollStatus();
</script>
</body>
</html>
)rawliteral";
