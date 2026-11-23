import gzip

html = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>NFC Studio • ESP32-C3</title>
<style>
:root {
  --bg: #07090e;
  --surface: #0f141f;
  --surface-2: #161e30;
  --surface-3: #1e2942;
  --border: rgba(255, 255, 255, 0.08);
  --cyan: #06b6d4;
  --cyan-glow: rgba(6, 182, 212, 0.25);
  --emerald: #10b981;
  --emerald-glow: rgba(16, 185, 129, 0.25);
  --amber: #f59e0b;
  --amber-glow: rgba(245, 158, 11, 0.25);
  --purple: #a855f7;
  --rose: #f43f5e;
  --text-main: #f8fafc;
  --text-muted: #94a3b8;
  --text-dim: #64748b;
  --radius: 14px;
}
* { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }
body {
  background: var(--bg);
  color: var(--text-main);
  font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 16px;
  background-image: 
    radial-gradient(ellipse 80% 50% at 50% -20%, rgba(6, 182, 212, 0.15), transparent 70%),
    radial-gradient(circle at 100% 100%, rgba(139, 92, 246, 0.08), transparent 50%);
}
.app {
  width: 100%;
  max-width: 500px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 4px 2px 8px;
}
.brand {
  display: flex;
  align-items: center;
  gap: 10px;
}
.logo-icon {
  width: 38px;
  height: 38px;
  border-radius: 10px;
  background: linear-gradient(135deg, #06b6d4, #3b82f6);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 0 20px rgba(6, 182, 212, 0.35);
}
.logo-icon svg { width: 22px; height: 22px; fill: none; stroke: #fff; stroke-width: 2.2; }
.brand h1 { font-size: 19px; font-weight: 800; letter-spacing: -0.3px; color: #fff; }
.brand span { font-size: 11px; font-weight: 600; color: var(--cyan); text-transform: uppercase; letter-spacing: 0.8px; }

.header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}
.pill-btn {
  background: var(--surface-2);
  border: 1px solid var(--border);
  color: var(--text-muted);
  font-size: 12px;
  font-weight: 600;
  padding: 6px 12px;
  border-radius: 20px;
  display: flex;
  align-items: center;
  gap: 6px;
  cursor: pointer;
  transition: all 0.2s ease;
}
.pill-btn:hover { background: var(--surface-3); color: #fff; }
.pill-btn.active { color: var(--cyan); border-color: rgba(6, 182, 212, 0.4); background: rgba(6, 182, 212, 0.1); }
.status-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--emerald);
  box-shadow: 0 0 8px var(--emerald);
  animation: pulse 2s infinite;
}
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }

.card {
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 18px;
  position: relative;
  overflow: hidden;
  box-shadow: 0 8px 30px rgba(0, 0, 0, 0.4);
  backdrop-filter: blur(12px);
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 14px;
}
.card-title {
  font-size: 12px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 1px;
  color: var(--text-dim);
  display: flex;
  align-items: center;
  gap: 6px;
}
.card-title svg { width: 14px; height: 14px; stroke: var(--text-dim); stroke-width: 2; fill: none; }

.radar-box {
  display: flex;
  align-items: center;
  gap: 14px;
  background: var(--surface-2);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 12px 14px;
  margin-bottom: 14px;
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.radar-box.detected {
  background: linear-gradient(135deg, rgba(16, 185, 129, 0.15), rgba(6, 182, 212, 0.1));
  border-color: rgba(16, 185, 129, 0.4);
  box-shadow: 0 0 25px rgba(16, 185, 129, 0.15);
}
.radar-icon {
  width: 44px;
  height: 44px;
  border-radius: 50%;
  background: var(--surface-3);
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  flex-shrink: 0;
  transition: all 0.3s ease;
}
.radar-box.detected .radar-icon {
  background: var(--emerald);
  box-shadow: 0 0 16px var(--emerald-glow);
}
.radar-icon svg {
  width: 22px;
  height: 22px;
  stroke: var(--text-muted);
  stroke-width: 2;
  fill: none;
}
.radar-box.detected .radar-icon svg {
  stroke: #07090e;
  stroke-width: 2.5;
}
.radar-info h2 { font-size: 15px; font-weight: 700; color: #fff; }
.radar-info p { font-size: 12px; color: var(--text-muted); margin-top: 1px; }

.detail-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
  margin-bottom: 12px;
}
.detail-box {
  background: var(--surface-2);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 10px 12px;
}
.detail-box.full { grid-column: span 2; }
.detail-label {
  font-size: 10px;
  font-weight: 700;
  text-transform: uppercase;
  color: var(--text-dim);
  letter-spacing: 0.8px;
  margin-bottom: 4px;
}
.detail-val {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
  font-size: 13px;
  font-weight: 600;
  color: var(--cyan);
  word-break: break-all;
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.copy-btn {
  background: none;
  border: none;
  color: var(--text-dim);
  cursor: pointer;
  padding: 2px 4px;
  display: flex;
  align-items: center;
  transition: color 0.2s;
}
.copy-btn:hover { color: #fff; }
.copy-btn svg { width: 14px; height: 14px; stroke: currentColor; fill: none; stroke-width: 2; }

.payload-box {
  background: #080c14;
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 12px;
  position: relative;
}
.payload-text {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
  font-size: 12px;
  color: #e2e8f0;
  line-height: 1.5;
  word-break: break-all;
  white-space: pre-wrap;
  max-height: 100px;
  overflow-y: auto;
}

.url-action-card {
  margin-top: 10px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: linear-gradient(135deg, rgba(6, 182, 212, 0.15), rgba(59, 130, 246, 0.15));
  border: 1px solid rgba(6, 182, 212, 0.35);
  border-radius: 8px;
  padding: 10px 14px;
}
.open-url-btn {
  background: linear-gradient(135deg, #06b6d4, #2563eb);
  color: #fff !important;
  font-size: 13px;
  font-weight: 700;
  text-decoration: none;
  padding: 6px 14px;
  border-radius: 6px;
  display: flex;
  align-items: center;
  gap: 6px;
  box-shadow: 0 2px 10px rgba(6, 182, 212, 0.3);
  transition: all 0.2s ease;
}
.open-url-btn:hover {
  transform: translateY(-1px);
  box-shadow: 0 4px 15px rgba(6, 182, 212, 0.45);
}
.open-url-btn svg { width: 14px; height: 14px; stroke: #fff; fill: none; stroke-width: 2.5; }

.auto-open-toggle {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: var(--text-muted);
  cursor: pointer;
}
.auto-open-toggle input { accent-color: var(--cyan); cursor: pointer; }

.dump-actions-bar {
  display: flex;
  gap: 8px;
  margin-top: 10px;
}
.btn-dump {
  flex: 1;
  background: var(--surface-3);
  border: 1px solid var(--border);
  color: var(--cyan);
  font-size: 12px;
  font-weight: 700;
  padding: 9px 12px;
  border-radius: 8px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  transition: all 0.2s ease;
}
.btn-dump:hover { background: rgba(6, 182, 212, 0.15); border-color: var(--cyan); color: #fff; }

.tabs {
  display: flex;
  background: var(--surface-2);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 3px;
  gap: 4px;
  margin-bottom: 14px;
}
.tab {
  flex: 1;
  background: none;
  border: none;
  color: var(--text-muted);
  font-size: 11px;
  font-weight: 700;
  padding: 8px 0;
  border-radius: 7px;
  cursor: pointer;
  transition: all 0.2s ease;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
}
.tab.active {
  background: var(--surface-3);
  color: #fff;
  box-shadow: 0 2px 8px rgba(0,0,0,0.3);
}
.tab.active.clone {
  background: rgba(168, 85, 247, 0.2);
  color: var(--purple);
}
.tab.active.wipe {
  background: rgba(244, 63, 94, 0.2);
  color: var(--rose);
}

.input-group {
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-bottom: 12px;
}
input[type="text"], textarea {
  width: 100%;
  background: #080c14;
  border: 1px solid var(--border);
  border-radius: 10px;
  color: #fff;
  font-size: 13px;
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
  padding: 12px 14px;
  outline: none;
  transition: all 0.2s ease;
}
input[type="text"]:focus, textarea:focus {
  border-color: var(--cyan);
  box-shadow: 0 0 0 3px var(--cyan-glow);
}

.btn-primary {
  width: 100%;
  background: linear-gradient(135deg, #06b6d4, #2563eb);
  color: #fff;
  border: none;
  border-radius: 10px;
  padding: 13px 16px;
  font-size: 14px;
  font-weight: 700;
  letter-spacing: 0.2px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  box-shadow: 0 4px 20px rgba(6, 182, 212, 0.35);
  transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
}
.btn-primary:hover {
  transform: translateY(-1px);
  box-shadow: 0 6px 25px rgba(6, 182, 212, 0.45);
}
.btn-primary:active {
  transform: translateY(1px);
}
.btn-purple {
  background: linear-gradient(135deg, #a855f7, #7c3aed);
  box-shadow: 0 4px 20px rgba(168, 85, 247, 0.35);
}
.btn-purple:hover {
  box-shadow: 0 6px 25px rgba(168, 85, 247, 0.45);
}
.btn-danger {
  background: linear-gradient(135deg, #f43f5e, #e11d48);
  box-shadow: 0 4px 20px rgba(244, 63, 94, 0.35);
}
.btn-danger:hover {
  box-shadow: 0 6px 25px rgba(244, 63, 94, 0.45);
}

.armed-banner {
  display: none;
  background: linear-gradient(135deg, rgba(245, 158, 11, 0.15), rgba(217, 119, 6, 0.25));
  border: 1px solid var(--amber);
  border-radius: 12px;
  padding: 14px 16px;
  margin-bottom: 12px;
  animation: pulse-border 1.8s infinite;
}
@keyframes pulse-border {
  0%, 100% { box-shadow: 0 0 15px var(--amber-glow); }
  50% { box-shadow: 0 0 30px var(--amber-glow); }
}
.armed-content {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.armed-left {
  display: flex;
  align-items: center;
  gap: 10px;
}
.armed-icon {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  background: var(--amber);
  display: flex;
  align-items: center;
  justify-content: center;
  color: #07090e;
  font-weight: 800;
}
.armed-text h4 { font-size: 13px; font-weight: 700; color: #fff; }
.armed-text p { font-size: 11px; color: #fde68a; }
.btn-cancel {
  background: rgba(255, 255, 255, 0.15);
  border: 1px solid rgba(255, 255, 255, 0.2);
  color: #fff;
  font-size: 11px;
  font-weight: 700;
  padding: 6px 12px;
  border-radius: 6px;
  cursor: pointer;
  transition: background 0.2s;
}
.btn-cancel:hover { background: rgba(255, 255, 255, 0.25); }

.status-msg {
  margin-top: 10px;
  padding: 8px 12px;
  border-radius: 8px;
  font-size: 12px;
  font-weight: 600;
  display: none;
  align-items: center;
  gap: 6px;
}
.status-msg.success { display: flex; background: rgba(16, 185, 129, 0.15); border: 1px solid var(--emerald); color: var(--emerald); }
.status-msg.error { display: flex; background: rgba(244, 63, 94, 0.15); border: 1px solid var(--rose); color: var(--rose); }

.file-drop-zone {
  border: 2px dashed var(--border);
  border-radius: 10px;
  padding: 16px;
  text-align: center;
  cursor: pointer;
  transition: all 0.2s ease;
  margin-bottom: 12px;
  background: #080c14;
}
.file-drop-zone:hover {
  border-color: var(--purple);
  background: rgba(168, 85, 247, 0.05);
}

footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 11px;
  color: var(--text-dim);
  padding: 6px 4px;
}
.stat-pill {
  background: var(--surface);
  border: 1px solid var(--border);
  padding: 4px 10px;
  border-radius: 20px;
  display: flex;
  align-items: center;
  gap: 6px;
}
</style>
</head>
<body>
<div class="app">
  <header>
    <div class="brand">
      <div class="logo-icon">
        <svg viewBox="0 0 24 24"><path d="M2 8.5C4 6 7.5 4.5 12 4.5s8 1.5 10 4M4.5 12.5C6 10.5 8.5 9.5 12 9.5s6 1 7.5 3M7.5 16.5c1-1 2.5-1.5 4.5-1.5s3.5.5 4.5 1.5M12 20h.01"/></svg>
      </div>
      <div>
        <h1>NFC Studio</h1>
        <span>ESP32-C3 &bull; Standalone</span>
      </div>
    </div>
    <div class="header-actions">
      <button id="sndBtn" class="pill-btn active" onclick="toggleSound()">
        <span id="sndIcon">&#128266;</span> Sound
      </button>
      <div class="stat-pill">
        <div class="status-dot"></div>
        <span id="hwStat">RC522 0x82</span>
      </div>
    </div>
  </header>

  <!-- Live Tag Inspector -->
  <div class="card">
    <div class="card-header">
      <span class="card-title">
        <svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/></svg>
        Live Field Inspector
      </span>
      <span id="tagTypeBadge" class="stat-pill" style="display:none; color:var(--cyan); font-weight:700;">NTAG213</span>
    </div>

    <!-- Live Radar State -->
    <div id="radarBox" class="radar-box">
      <div class="radar-icon">
        <svg viewBox="0 0 24 24"><rect x="2" y="4" width="20" height="16" rx="2"/><circle cx="12" cy="12" r="2"/><path d="M6 12h.01M18 12h.01"/></svg>
      </div>
      <div class="radar-info">
        <h2 id="radarTitle">Searching for NFC Tag...</h2>
        <p id="radarDesc">Hold any NFC card, sticker, or fob near the reader</p>
      </div>
    </div>

    <!-- Details Grid -->
    <div class="detail-grid">
      <div class="detail-box">
        <div class="detail-label">UID / Serial</div>
        <div class="detail-val">
          <span id="uidVal">--:--:--:--</span>
          <button class="copy-btn" onclick="copyText('uidVal')" title="Copy UID">
            <svg viewBox="0 0 24 24"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>
          </button>
        </div>
      </div>
      <div class="detail-box">
        <div class="detail-label">SAK / Protocol</div>
        <div class="detail-val" id="sakVal">0x00 • ISO 14443-A</div>
      </div>
      <div class="detail-box full">
        <div class="detail-label">Tag Data / NDEF Payload</div>
        <div class="payload-box">
          <div id="payloadVal" class="payload-text">No tag scanned yet</div>
          <div id="urlCard" class="url-action-card" style="display:none;">
            <a id="urlLink" class="open-url-btn" href="#" target="_blank">
              <span>Open Link</span>
              <svg viewBox="0 0 24 24"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6M15 3h6v6M10 14L21 3"/></svg>
            </a>
            <label class="auto-open-toggle">
              <input type="checkbox" id="autoOpenCheck" onchange="toggleAutoOpen()">
              <span>⚡ Auto-open on Scan</span>
            </label>
          </div>
        </div>

        <!-- Quick Dump Download Action -->
        <div class="dump-actions-bar">
          <button class="btn-dump" onclick="downloadTagDump()">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3"/></svg>
            Export Memory Dump (.json)
          </button>
        </div>
      </div>
    </div>
  </div>

  <!-- Write Station -->
  <div class="card">
    <div class="card-header">
      <span class="card-title">
        <svg viewBox="0 0 24 24"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>
        Write &amp; Clone Station
      </span>
    </div>

    <!-- Armed Status Banner -->
    <div id="armedBanner" class="armed-banner">
      <div class="armed-content">
        <div class="armed-left">
          <div class="armed-icon">&excl;</div>
          <div class="armed-text">
            <h4 id="armedTitle">SENSOR ARMED</h4>
            <p id="armedSub">Hold target tag to reader to complete write</p>
          </div>
        </div>
        <button class="btn-cancel" onclick="cancelArm()">Cancel</button>
      </div>
    </div>

    <!-- Action Tabs -->
    <div class="tabs">
      <button id="tabUrl" class="tab active" onclick="switchTab('url')">URL / Link</button>
      <button id="tabText" class="tab" onclick="switchTab('text')">Plain Text</button>
      <button id="tabClone" class="tab clone" onclick="switchTab('clone')">Dump / Clone</button>
      <button id="tabWipe" class="tab wipe" onclick="switchTab('wipe')">Format / Wipe</button>
    </div>

    <!-- URL Tab Content -->
    <div id="contentUrl">
      <div class="input-group">
        <input type="text" id="urlInput" placeholder="https://example.com/profile" value="https://">
      </div>
      <button class="btn-primary" onclick="armWrite('url')">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M12 5v14M5 12h14"/></svg>
        Arm to Write URL
      </button>
    </div>

    <!-- Text Tab Content -->
    <div id="contentText" style="display:none;">
      <div class="input-group">
        <textarea id="textInput" rows="3" placeholder="Enter custom note, credentials, or text payload..."></textarea>
      </div>
      <button class="btn-primary" onclick="armWrite('text')">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M12 5v14M5 12h14"/></svg>
        Arm to Write Text
      </button>
    </div>

    <!-- Clone / Dump Tab Content -->
    <div id="contentClone" style="display:none;">
      <div class="file-drop-zone" onclick="document.getElementById('dumpFileInput').click()">
        <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="var(--purple)" stroke-width="2" style="margin-bottom:6px;"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M17 8l-5-5-5 5M12 3v12"/></svg>
        <p style="font-size:12px;font-weight:700;color:#fff;" id="dumpFileLabel">Upload Dump File (.json)</p>
        <p style="font-size:11px;color:var(--text-dim);margin-top:2px;">Select previously exported JSON memory dump</p>
        <input type="file" id="dumpFileInput" accept=".json" style="display:none;" onchange="handleDumpFileSelect(event)">
      </div>
      <div class="input-group">
        <textarea id="dumpJsonInput" rows="3" placeholder='Paste JSON dump data here or select file above...'></textarea>
      </div>
      <button class="btn-primary btn-purple" onclick="armWrite('dump')">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M16 16v1a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2h11a2 2 0 0 1 2 2v1M18 8l4 4-4 4M8 12h14"/></svg>
        Arm to Restore / Clone Dump
      </button>
    </div>

    <!-- Wipe Tab Content -->
    <div id="contentWipe" style="display:none;">
      <p style="font-size:12px;color:var(--text-muted);margin-bottom:12px;line-height:1.5;">
        Erases NDEF data pages and resets standard user blocks to factory zeros. Compatible with NTAG213/215/216 and MIFARE Classic.
      </p>
      <button class="btn-primary btn-danger" onclick="armWrite('wipe')">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
        Arm Tag Wipe
      </button>
    </div>

    <!-- Feedback Message -->
    <div id="statusMsg" class="status-msg"></div>
  </div>

  <!-- Footer Stats -->
  <footer>
    <div class="stat-pill">
      <span id="uptimeVal">Uptime: 0s</span>
    </div>
    <div class="stat-pill">
      <span id="ipVal">IP: <b>192.168.1.37</b></span>
    </div>
  </footer>
</div>

<script>
let currentTab = 'url';
let soundEnabled = true;
let autoOpenEnabled = localStorage.getItem('nfc_auto_open') === 'true';
let lastScanId = 0;
let audioCtx = null;
let currentTagDump = null;

document.getElementById('autoOpenCheck').checked = autoOpenEnabled;

function toggleAutoOpen() {
  autoOpenEnabled = document.getElementById('autoOpenCheck').checked;
  localStorage.setItem('nfc_auto_open', autoOpenEnabled ? 'true' : 'false');
  playTone(800, 0.04);
}

function toggleSound() {
  soundEnabled = !soundEnabled;
  const btn = document.getElementById('sndBtn');
  btn.classList.toggle('active', soundEnabled);
  document.getElementById('sndIcon').innerHTML = soundEnabled ? '&#128266;' : '&#128263;';
  if (soundEnabled) playTone(880, 0.05);
}

function playTone(freq, duration, type) {
  duration = duration || 0.08;
  type = type || 'sine';
  if (!soundEnabled) return;
  try {
    audioCtx = audioCtx || new (window.AudioContext || window.webkitAudioContext)();
    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.type = type;
    osc.frequency.value = freq;
    gain.gain.setValueAtTime(0.08, audioCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + duration);
    osc.connect(gain);
    gain.connect(audioCtx.destination);
    osc.start();
    osc.stop(audioCtx.currentTime + duration);
  } catch(e) {}
}

function playSuccessChime() {
  playTone(587.33, 0.06);
  setTimeout(function() { playTone(880, 0.12); }, 70);
}

function playCelebration() {
  playTone(523.25, 0.08);
  setTimeout(function() { playTone(659.25, 0.08); }, 80);
  setTimeout(function() { playTone(783.99, 0.15); }, 160);
}

function switchTab(tab) {
  currentTab = tab;
  document.getElementById('tabUrl').classList.toggle('active', tab === 'url');
  document.getElementById('tabText').classList.toggle('active', tab === 'text');
  document.getElementById('tabClone').classList.toggle('active', tab === 'clone');
  document.getElementById('tabWipe').classList.toggle('active', tab === 'wipe');
  
  document.getElementById('contentUrl').style.display = tab === 'url' ? 'block' : 'none';
  document.getElementById('contentText').style.display = tab === 'text' ? 'block' : 'none';
  document.getElementById('contentClone').style.display = tab === 'clone' ? 'block' : 'none';
  document.getElementById('contentWipe').style.display = tab === 'wipe' ? 'block' : 'none';
  playTone(600, 0.03);
}

function handleDumpFileSelect(e) {
  const file = e.target.files[0];
  if (!file) return;
  document.getElementById('dumpFileLabel').textContent = file.name;
  const reader = new FileReader();
  reader.onload = function(evt) {
    document.getElementById('dumpJsonInput').value = evt.target.result;
    playTone(900, 0.05);
  };
  reader.readAsText(file);
}

async function downloadTagDump() {
  try {
    const res = await fetch('/api/dump');
    const dump = await res.json();
    if (!dump || (!dump.pages && !dump.blocks)) {
      alert('No tag dump available. Tap a tag on the reader first.');
      return;
    }
    const blob = new Blob([JSON.stringify(dump, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'nfc_dump_' + (dump.uid ? dump.uid.replace(/:/g, '') : 'tag') + '.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    playCelebration();
  } catch(e) {
    alert('Failed to download dump: ' + e);
  }
}

async function armWrite(type) {
  let payload = '';
  if (type === 'url') {
    payload = document.getElementById('urlInput').value.trim();
    if (!payload || payload === 'https://' || payload === 'http://') {
      alert('Please enter a valid URL.');
      return;
    }
  } else if (type === 'text') {
    payload = document.getElementById('textInput').value.trim();
    if (!payload) {
      alert('Please enter text to write.');
      return;
    }
  } else if (type === 'dump') {
    payload = document.getElementById('dumpJsonInput').value.trim();
    if (!payload) {
      alert('Please select or paste a JSON dump.');
      return;
    }
    try { JSON.parse(payload); } catch(err) {
      alert('Invalid JSON format in dump payload.');
      return;
    }
  }

  try {
    const res = await fetch('/api/arm', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ type: type, payload: payload })
    });
    const data = await res.json();
    if (data.ok) {
      document.getElementById('armedBanner').style.display = 'block';
      document.getElementById('armedTitle').textContent = 'ARMED: ' + type.toUpperCase();
      document.getElementById('armedSub').textContent = 'Tap card to ' + (type === 'wipe' ? 'wipe' : type === 'dump' ? 'restore dump' : 'write');
      playTone(950, 0.08);
      poll();
    }
  } catch(e) {
    alert('Failed to arm sensor: ' + e);
  }
}

async function cancelArm() {
  try {
    await fetch('/api/cancel', { method: 'POST' });
    playTone(400, 0.06);
    poll();
  } catch(e) {}
}

function copyText(elemId) {
  const text = document.getElementById(elemId).textContent;
  if (text && text !== '--:--:--:--') {
    navigator.clipboard.writeText(text);
    playTone(1000, 0.04);
  }
}

async function poll() {
  try {
    const res = await fetch('/api/status');
    const data = await res.json();

    document.getElementById('hwStat').textContent = 'RC522 ' + (data.rc522 || '0x82');

    const radarBox = document.getElementById('radarBox');
    const radarTitle = document.getElementById('radarTitle');
    const radarDesc = document.getElementById('radarDesc');
    const badge = document.getElementById('tagTypeBadge');

    if (data.card_present) {
      radarBox.classList.add('detected');
      radarTitle.textContent = 'Tag on Sensor';
      radarDesc.textContent = (data.tag.type || 'NFC Tag') + ' in active RF field';
      badge.style.display = 'inline-flex';
      badge.textContent = data.tag.type || 'NFC';
    } else {
      radarBox.classList.remove('detected');
      radarTitle.textContent = data.scan_id > 0 ? 'Tag Lifted / Ready' : 'Searching for NFC Tag...';
      radarDesc.textContent = 'Hold any NFC card, sticker, or fob near the reader';
    }

    if (data.tag && data.tag.uid && data.tag.uid !== '--:--:--:--') {
      document.getElementById('uidVal').textContent = data.tag.uid;
      document.getElementById('sakVal').textContent = '0x' + data.tag.sak + ' • ' + data.tag.type;
      
      const payload = data.tag.payload || '(Empty Tag)';
      document.getElementById('payloadVal').textContent = payload;

      const isUrl = payload.startsWith('http://') || payload.startsWith('https://');
      const urlCard = document.getElementById('urlCard');
      const urlLink = document.getElementById('urlLink');
      if (isUrl) {
        urlCard.style.display = 'flex';
        urlLink.href = payload;

        // Auto-open URL when new tag is scanned
        if (autoOpenEnabled && data.scan_id > lastScanId) {
          window.open(payload, '_blank');
        }
      } else {
        urlCard.style.display = 'none';
      }
    }

    if (data.scan_id > lastScanId) {
      lastScanId = data.scan_id;
      if (data.last_action_status && data.last_action_status.includes('Success')) {
        playCelebration();
      } else {
        playSuccessChime();
      }
    }

    const armedBanner = document.getElementById('armedBanner');
    if (data.armed) {
      armedBanner.style.display = 'block';
      document.getElementById('armedTitle').textContent = 'ARMED: ' + data.armed_type.toUpperCase();
      document.getElementById('armedSub').textContent = 'Tap card to ' + (data.armed_type === 'wipe' ? 'wipe' : data.armed_type === 'dump' ? 'restore dump' : 'write');
    } else {
      armedBanner.style.display = 'none';
    }

    const msg = document.getElementById('statusMsg');
    if (data.last_action_status && data.last_action_status !== 'Idle') {
      const isSuccess = data.last_action_status.includes('Success');
      msg.className = 'status-msg ' + (isSuccess ? 'success' : 'error');
      msg.innerHTML = (isSuccess ? '&#10004; ' : '&#9888; ') + data.last_action_status;
    }

    if (data.uptime) {
      document.getElementById('uptimeVal').textContent = 'Uptime: ' + data.uptime + 's';
    }
    if (data.ip) {
      document.getElementById('ipVal').innerHTML = 'IP: <b>' + data.ip + '</b>';
    }

  } catch(e) {}
  setTimeout(poll, 100);
}

poll();
</script>
</body>
</html>"""

gz_data = gzip.compress(html.encode('utf-8'), compresslevel=9)
print(f'Original size: {len(html)} bytes | Gzipped size: {len(gz_data)} bytes')

with open('src/html_gz.h', 'w') as f:
    f.write('// Auto-generated pre-compressed GZIP HTML\n#pragma once\n#include <Arduino.h>\n\n')
    f.write(f'const uint32_t INDEX_HTML_GZ_LEN = {len(gz_data)};\n')
    f.write('const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n')
    for i in range(0, len(gz_data), 16):
        chunk = gz_data[i:i+16]
        hex_str = ', '.join([f'0x{b:02X}' for b in chunk])
        f.write(f'    {hex_str},\n')
    f.write('};\n')
print('src/html_gz.h written successfully!')
