#ifndef SLAVE_PAGES_H
#define SLAVE_PAGES_H

#ifndef PROGMEM
#define PROGMEM
#endif

static const char kSlavePageHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1" />
  <meta http-equiv="Cache-Control" content="no-store">
  <title>Boat Gen Control (Slave)</title>

  <style>
    * { box-sizing: border-box; }
    html, body { height: 100%; overflow: hidden; }
    :root { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background:#0c1b2a; color:#e9f1f7; }
    body { margin:0; min-height:100vh; display:flex; flex-direction:column; }
    main { flex:1; display:flex; flex-direction:column; min-height:0; }
    header { position:relative; background:#132a45; padding:18px; text-align:center; box-shadow:0 2px 12px rgba(0,0,0,0.4); }
    header h1 { margin:0; font-size:1.2rem; }
    header p { margin:6px 0 0; color:#9bc5d6; font-size:0.94rem; }

    .page {
      -webkit-overflow-scrolling: touch;
      flex: 1;
      min-height:0;
      display: none;
      padding: 16px;
      padding-bottom: 80px;
      overflow: auto;
    }
    .page.active { display:block; }

    .control-container,
    .debug-container,
    .config-container {
      width: 100%;
      max-width: 480px;
      margin: 0 auto;
    }

    .card {
      background:rgba(255,255,255,0.06);
      border:1px solid rgba(255,255,255,0.08);
      border-radius:18px;
      padding:18px;
      margin-bottom:16px;
    }

    .button {
      width:100%;
      padding:14px 18px;
      border:none;
      border-radius:14px;
      margin-top:12px;
      font-size:1rem;
      cursor:pointer;
      color:#fff;
      background:#1976d2;
    }
    .button.secondary { background:#374d68; }

    /* ⭐ Added spinner CSS */
    .spinner {
      border: 3px solid rgba(255,255,255,0.3);
      border-top: 3px solid white;
      border-radius: 50%;
      width: 16px;
      height: 16px;
      animation: spin 0.8s linear infinite;
      display: inline-block;
      vertical-align: middle;
      margin-right: 8px;
    }
    @keyframes spin {
      from { transform: rotate(0deg); }
      to   { transform: rotate(360deg); }
    }
    @keyframes buttonBlink {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.5; }
    }
    .blink-feedback {
      animation: buttonBlink 0.6s ease-in-out;
    }

    .status-row {
      display:flex;
      justify-content:space-between;
      margin:10px 0;
    }
    .status-label { color:#99b7cd; }
    .status-value { font-weight:700; text-transform: uppercase; }
    .status-value {
      display:inline-flex;
      align-items:center;
      gap:0.5rem;
    }
    .status-value::before {
      content: '●';
      font-size:0.95rem;
      opacity: 0.9;
    }
    .status-value.status-red { color:#ff4d4f; }
    .status-value.status-yellow { color:#f5d442; }
    .status-value.status-green { color:#7ed321; }
    .status-value.status-purple { color:#b47cff; }

    .header-statuses {
      display:flex;
      flex-direction:column;
      gap:0.5rem;
      margin-top:0.35rem;
    }

    .header-reload {
      position:absolute;
      top:10px;
      right:14px;
      width:38px;
      height:38px;
      border-radius:50%;
      border:1px solid rgba(255,255,255,0.2);
      background:rgba(255,255,255,0.08);
      color:#e9f1f7;
      font-size:1.15rem;
      cursor:pointer;
      display:flex;
      align-items:center;
      justify-content:center;
      -webkit-tap-highlight-color: transparent;
    }
    .header-reload:active {
      transform:scale(0.96);
    }

    .socket-status {
      font-weight:700;
      text-transform: uppercase;
      display:inline-flex;
      align-items:center;
      gap:0.5rem;
    }
    .socket-status::before {
      content: '●';
      font-size:0.95rem;
    }
    .socket-status.disconnected { color:#ff4d4f; }
    .socket-status.connecting { color:#f5d442; }
    .socket-status.connected { color:#7ed321; }

    .wifi-ssid {
      font-weight:400;
      text-transform:none;
      color:#a3d4ff;
      display:inline-flex;
      align-items:center;
      gap:0.25rem;
    }
    .wifi-ssid::before {
      content: '📶';
    }
    .wifi-ssid.hidden {
      display:none;
    }

    .nav-bar {
      position: fixed;
      bottom: 20px;
      left: 0;
      width: 100%;
      display: flex;
      justify-content: space-around;
      background: #132a45;
      padding: 12px 0;
      z-index: 999;
    }
    .nav-button {
      color:#cbd8e6;
      background:none;
      border:none;
      font-size:0.95rem;
      cursor:pointer;
    }
    .nav-button.active { color:#ffffff; font-weight:700; }

    .log-window {
      background:#050d17;
      border:1px solid #23405c;
      border-radius:16px;
      padding:12px;
      height: calc(100dvh - 370px);
      overflow:auto;
      font-family:monospace;
      font-size:0.85rem;
    }

    .input-row { margin-bottom:14px; }
    .input-row label { display:block; margin-bottom:6px; color:#9bb5c9; }
    .master-toggle {
      display:inline-flex;
      align-items:center;
      gap:8px;
      font-size:14px;
      margin:0;
      white-space:nowrap;
      line-height:1.2;
      color:#eef3f6;
    }
    .master-toggle input[type="checkbox"] {
      appearance:none;
      -webkit-appearance:none;
      width:16px;
      height:16px;
      min-width:16px;
      border:2px solid #9bc5d6;
      border-radius:4px;
      background:transparent;
      cursor:pointer;
      margin:0;
      display:inline-block;
      vertical-align:middle;
      flex-shrink:0;
    }
    .master-toggle input[type="checkbox"]:checked {
      background:#1976d2 url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path fill="white" d="M13.78 4.22a.75.75 0 010 1.06l-7.25 7.25a.75.75 0 01-1.06 0L2.22 9.28a.75.75 0 011.06-1.06L6 11.94l6.72-6.72a.75.75 0 011.06 0z"/></svg>') no-repeat center center;
      border-color:#1976d2;
      background-size:12px 12px;
    }
    .input-row input {
      width:100%;
      font-size:1.1rem;
      padding:4px 8px;
      border-radius:12px;
      border:1px solid #2c4b67;
      background:#0f2233;
      color:#eef3f6;
    }

    .input-flex {
      display:flex;
      align-items:center;
      gap:10px;
    }
    .input-flex input {
      flex:1;
      min-width:0;
      font-size:1.1rem;
      padding:4px 8px;
    }
    .eye-btn {
      background:none;
      border:none;
      color:#9bb5c9;
      font-size:1.2rem;
      cursor:pointer;
      padding:6px;
    }
    .save-inline {
      padding:10px 14px;
      border-radius:12px;
      background:#1976d2;
      color:white;
      border:none;
      cursor:pointer;
      white-space:nowrap;
    }

    .config-tabs {
      display:flex;
      gap:8px;
      margin-bottom:16px;
      border-bottom:2px solid rgba(255,255,255,0.1);
    }
    .config-tab-btn {
      flex:1;
      padding:12px 16px;
      background:none;
      border:none;
      color:#9bb5c9;
      cursor:pointer;
      font-size:1rem;
      border-bottom:3px solid transparent;
      transition:all 0.3s ease;
    }
    .config-tab-btn.active {
      color:#ffffff;
      border-bottom-color:#1976d2;
      font-weight:600;
    }

    .config-tab-content {
      display:none;
    }
    .config-tab-content.active {
      display:block;
    }

    .progress-bar {
      width:100%;
      height:8px;
      background:rgba(255,255,255,0.1);
      border-radius:4px;
      overflow:hidden;
      margin-top:8px;
    }
    .progress-bar-fill {
      height:100%;
      background:#1976d2;
      width:0%;
      transition:width 0.3s ease;
    }

    .update-status {
      position: fixed;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      z-index: 10;
      min-width: 300px;
      max-width: 90vw;
      padding: 10px 12px;
      background: #0f2233;
      border: 1px solid #1976d2;
      border-radius: 8px;
      font-size: 0.9rem;
      color: #9bc5d6;
      text-align: center;
      box-shadow: 0 4px 6px rgba(0, 0, 0, 0.2);
    }
    
    .confirmation-dialog {
      position: fixed;
      top: 0;
      left: 0;
      right: 0;
      bottom: 0;
      background: #000000;
      display: none;
      z-index: 999;
      align-items: center;
      justify-content: center;
    }
    
    .confirmation-content {
      position: fixed;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      z-index: 1000;
      min-width: 300px;
      max-width: 90vw;
      padding: 20px;
      background: #132a45;
      border: 1px solid #1976d2;
      border-radius: 12px;
      text-align: center;
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
    }
    
    .confirmation-content p {
      margin: 0 0 16px 0;
      color: #e9f1f7;
      font-size: 1rem;
    }
    
    .confirmation-buttons {
      display: flex;
      gap: 12px;
      justify-content: center;
    }
    
    .confirmation-buttons .button {
      flex: 1;
      margin: 0;
      padding: 12px 16px;
    }
  </style>
</head>

<body>
  <!-- Update Status Popup (fixed to viewport) -->
  <div id="updateStatus" class="update-status" style="display:none;"></div>
  
  <!-- Firmware Update Confirmation Dialog -->
  <div id="confirmationDialog" class="confirmation-dialog" style="display:none;">
    <div class="confirmation-content">
      <p>Start firmware update? Device will reboot after update.</p>
      <div class="confirmation-buttons">
        <button class="button secondary" onclick="cancelFirmwareUpdate()">Cancel</button>
        <button class="button" onclick="confirmFirmwareUpdate()">Update</button>
      </div>
    </div>
  </div>

  <header>
    <button class="header-reload" type="button" title="Reload page" aria-label="Reload page" onclick="reloadPage()">&#x21bb;</button>
    <h1>R33 Boat Controller (Slave)</h1>
    <div class="header-statuses">
      <p id="socketStatus" class="socket-status disconnected">Browser: disconnected</p>
      <p id="masterStatus" class="socket-status disconnected">Master: disconnected</p>
    </div>
  </header>

  <main>

    <!-- CONTROL PAGE -->
    <section id="controlPage" class="page active">
      <div class="page-container control-container">

        <div class="card">
          <div class="status-row">
            <span class="status-label">Generator</span>
            <span id="generatorState" class="status-value">unknown</span>
          </div>

          <!-- ⭐ IDs added -->
          <button class="button" id="genStartButton" onclick="startGenerator()">Start Gen</button>
          <button class="button secondary" id="genStopButton" onclick="stopGenerator()">Stop Gen</button>
        </div>

        <div class="card">
          <div class="status-row">
            <span class="status-label">AC Voltage</span>
            <span id="acVoltage" class="status-value">--</span>
          </div>
          <div class="status-row">
            <span class="status-label">AC Current</span>
            <span id="acCurrent" class="status-value">--</span>
          </div>
          <div class="status-row">
            <span class="status-label">AC Frequency</span>
            <span id="acFrequency" class="status-value">--</span>
          </div>
        </div>

      </div>
    </section>

    <!-- DEBUG PAGE -->
    <section id="debugPage" class="page">
      <div class="page-container debug-container">
        <div class="card">
          <div class="status-row">
            <span class="status-label">Debug Log</span>
            <span id="debugCount" class="status-value">0 messages</span>
          </div>

          <button class="button secondary" onclick="clearDebugLog()" style="margin-bottom:12px;">
            Clear Debug Messages
          </button>

          <div id="debugLog" class="log-window"></div>
        </div>
      </div>
    </section>

    <!-- CONFIG PAGE -->
    <section id="configPage" class="page">
      <div class="page-container config-container">

        <div class="config-tabs">
          <button class="config-tab-btn active" onclick="switchConfigTab('config')">Configuration</button>
          <button class="config-tab-btn" onclick="switchConfigTab('updates')">Updates</button>
        </div>

        <!-- CONFIGURATION TAB -->
        <div id="configTab" class="config-tab-content active">
          <div class="card">
            <div class="input-row">
              <label for="isMaster" class="master-toggle">
                <span>Master Device</span>
                <input id="isMaster" type="checkbox" />
              </label>
            </div>

            <div class="input-row">
              <label for="ssid">WiFi SSID</label>
              <input id="ssid" type="text" placeholder="Network name" />
            </div>

            <div class="input-row">
              <label for="password">WiFi Password</label>
              <div class="input-flex">
                <input id="password" type="password" placeholder="Network password" />
                <button class="eye-btn" onclick="togglePassword('password', this)">👁</button>
                <button class="save-inline" onclick="saveSTAWifiConfig()">Save</button>
              </div>
            </div>
          </div>

          <div class="card">
            <div class="input-row">
              <label for="ap_ssid">AP WiFi SSID</label>
              <input id="ap_ssid" type="text" placeholder="Network name" />
            </div>

            <div class="input-row">
              <label for="ap_password">AP WiFi Password</label>
              <div class="input-flex">
                <input id="ap_password" type="password" placeholder="Network password" />
                <button class="eye-btn" onclick="togglePassword('ap_password', this)">👁</button>
                <button class="save-inline" onclick="saveAPWifiConfig()">Save</button>
              </div>
            </div>

            <div class="input-row"><label for="ap_ipAddress">AP WiFi ADDRESS</label><input id="ap_ipAddress" type="text" placeholder="x.x.x.x" /></div>
            <div class="input-row"><label for="ap_gateway">AP WiFi GATEWAY</label><input id="ap_gateway" type="text" placeholder="x.x.x.x" /></div>
            <div class="input-row"><label for="ap_netmask">AP WiFi NETMASK</label><input id="ap_netmask" type="text" placeholder="x.x.x.x" /></div>
          </div>
        </div>

        <!-- UPDATES TAB -->
        <div id="updatesTab" class="config-tab-content">
          <div class="card">
            <div class="status-row">
              <span class="status-label">Current Version</span>
              <span id="currentVersion" class="status-value">--</span>
            </div>
            <div class="status-row">
              <span class="status-label">Available Version</span>
              <span id="availableVersion" class="status-value">--</span>
            </div>

            <div class="input-row">
              <label for="updateServerUrl">Update Server URL</label>
              <input id="updateServerUrl" type="text" placeholder="http://192.168.1.100:8000" autocomplete="off" spellcheck="false" />
            </div>

            <button class="button secondary" onclick="checkForUpdates()">Check for Updates</button>
            <button class="button" id="updateBtn" onclick="startFirmwareUpdate()" style="display:none;">Update Now</button>

            <div id="updateProgress" style="display:none; margin-top:16px;">
              <div class="status-row">
                <span class="status-label">Downloading</span>
                <span id="updatePercentage" class="status-value">0%</span>
              </div>
              <div class="progress-bar">
                <div id="progressFill" class="progress-bar-fill"></div>
              </div>
            </div>
          </div>
        </div>

      </div>
    </section>

  </main>

  <nav class="nav-bar">
    <button class="nav-button active" data-page="controlPage" onclick="showPage('controlPage')">Control</button>
    <button class="nav-button" data-page="debugPage" onclick="showPage('debugPage')">Debug</button>
    <button class="nav-button" data-page="configPage" onclick="showPage('configPage')">Config</button>
  </nav>
  <script>
    let socket = null;
    let pageIndex = 0;
    const pages = ['controlPage','debugPage','configPage'];
    let touchStartX = 0;
    let debugMessageCounter = 0;
    let autoScroll = true;
    let lastDebugId = Number(localStorage.getItem("lastDebugId") || 0);
    let hasEverConnected = false;
    let domReady = false;
    let socketReady = false;

    /* ---------------------------------------------------------
       PENDING ACTION FLAGS + LAST KNOWN STATE
    --------------------------------------------------------- */
    let pendingStart = false;
    let pendingStop  = false;
    let ignoreGenState = false;
    let lastKnownState = "UNKNOWN";

    /* ---------------------------------------------------------
       SPINNER + BUTTON HELPERS
    --------------------------------------------------------- */
    function setButtonLoading(btn, text) {
      btn.disabled = true;
      btn.classList.add("secondary");
      btn.innerHTML = `<span class="spinner"></span>${text}`;
    }

    function resetButton(btn, text, isPrimary=true) {
      btn.disabled = false;
      btn.classList.toggle("secondary", !isPrimary);
      btn.innerHTML = text;
    }

    /* ---------------------------------------------------------
       INITIAL UI STATE
    --------------------------------------------------------- */
    window.addEventListener('DOMContentLoaded', () => {
      const startBtn = document.getElementById("genStartButton");
      const stopBtn  = document.getElementById("genStopButton");

      resetButton(startBtn, "Start Gen", true);
      stopBtn.disabled = true;
      stopBtn.classList.add("secondary");
      stopBtn.innerHTML = "Stop Gen";

      pendingStart = false;
      pendingStop  = false;
      lastKnownState = "UNKNOWN";
    });

    /* ---------------------------------------------------------
       PAGE + UI BASICS
    --------------------------------------------------------- */
    function setSocketStatus(text, ssid) {
      const status = document.getElementById('socketStatus');
      if (!status) return;
      const normalized = String(text || 'disconnected').toLowerCase();
      let wifiSpan = status.querySelector('.wifi-ssid');
      if (!wifiSpan) {
        wifiSpan = document.createElement('span');
        wifiSpan.className = 'wifi-ssid hidden';
      }
      status.textContent = 'Browser: ' + String(text || 'disconnected').toUpperCase();
      status.appendChild(wifiSpan);
      if (ssid) {
        wifiSpan.textContent = ssid;
        wifiSpan.classList.remove('hidden');
      } else {
        wifiSpan.textContent = '';
        wifiSpan.classList.add('hidden');
      }
      status.className = 'socket-status';
      status.classList.toggle('disconnected', normalized === 'disconnected');
      status.classList.toggle('connecting', normalized === 'connecting');
      status.classList.toggle('connected', normalized === 'connected');
    }

    function setMasterStatus(text) {
      const status = document.getElementById('masterStatus');
      if (!status) return;
      status.textContent = 'Master: ' + String(text || 'disconnected').toUpperCase();
      status.className = 'socket-status';
      status.classList.toggle('disconnected', String(text || 'disconnected').toLowerCase() === 'disconnected');
      status.classList.toggle('connecting', String(text || 'disconnected').toLowerCase() === 'connecting');
      status.classList.toggle('connected', String(text || 'disconnected').toLowerCase() === 'connected');
    }

    function setStatusText(id, text) {
      const status = document.getElementById(id);
      const value = String(text || '').trim();
      const normalized = value.toLowerCase().replace(/[_-]/g, ' ').trim();
      status.textContent = value;
      status.className = 'status-value';

      if (normalized === 'not running' || normalized === 'closed' || normalized === 'disconnected' || normalized === 'error' || normalized === 'unknown') {
        status.classList.add('status-red');
      } else if (normalized === 'starting' || normalized === 'stopping' || normalized === 'opening' || normalized === 'closing' || normalized === 'connecting' || normalized === 'reconnecting') {
        status.classList.add('status-yellow');
      } else if (normalized === 'running' || normalized === 'open' || normalized === 'connected') {
        status.classList.add('status-green');
      } else if (normalized === 'idle') {
        status.classList.add('status-purple');
      }
    }

    function togglePassword(id, btn) {
      const field = document.getElementById(id);
      field.type = field.type === "password" ? "text" : "password";
      btn.textContent = field.type === "password" ? "👁" : "🙈";
    }

    function reloadPage() {
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.close();
      }
      window.location.reload();
    }

    function showPage(id) {
      pages.forEach(page =>
        document.getElementById(page).classList.toggle('active', page === id)
      );
      document.querySelectorAll('.nav-button').forEach(btn =>
        btn.classList.toggle('active', btn.dataset.page === id)
      );
      pageIndex = pages.indexOf(id);
    }

    function swipePage(direction) {
      pageIndex = (pageIndex + direction + pages.length) % pages.length;
      showPage(pages[pageIndex]);
    }

    /* ---------------------------------------------------------
       UPDATED GENERATOR BUTTON LOGIC
    --------------------------------------------------------- */
    let startTimeout = null;
    let stopTimeout  = null;

    function updateGenButtons(state) {
      const startBtn = document.getElementById("genStartButton");
      const stopBtn  = document.getElementById("genStopButton");

      lastKnownState = state;

      if (pendingStart) {
        if (state === "RUNNING") {
          pendingStart = false;
          if (startTimeout) clearTimeout(startTimeout);

          startBtn.disabled = true;
          startBtn.classList.add("secondary");
          startBtn.innerHTML = "Start Gen";

          resetButton(stopBtn, "Stop Gen", false);
        }
        if (state === "STARTING") {
          pendingStart = false;
          if (startTimeout) clearTimeout(startTimeout);
        }
        return;
      }

      if (pendingStop) {
        if (state === "NOT RUNNING") {
          pendingStop = false;
          if (stopTimeout) clearTimeout(stopTimeout);

          stopBtn.disabled = true;
          stopBtn.classList.add("secondary");
          stopBtn.innerHTML = "Stop Gen";

          resetButton(startBtn, "Start Gen", true);
        }
        if (state === "STOPPING") {
          pendingStop = false;
          if (stopTimeout) clearTimeout(stopTimeout);
        }
        return;
      }

      if (state === "NOT RUNNING") {
        resetButton(startBtn, "Start Gen", true);
        stopBtn.disabled = true;
        stopBtn.classList.add("secondary");
        stopBtn.innerHTML = "Stop Gen";
      }
      else if (state === "RUNNING") {
        startBtn.disabled = true;
        startBtn.classList.add("secondary");
        startBtn.innerHTML = "Start Gen";

        resetButton(stopBtn, "Stop Gen", true);
      }
      else if (state === "STARTING") {
        setButtonLoading(startBtn, "Starting...");
        stopBtn.disabled = true;
        stopBtn.classList.add("secondary");
      }
      else if (state === "STOPPING") {
        setButtonLoading(stopBtn, "Stopping...");
        startBtn.disabled = true;
        startBtn.classList.add("secondary");
      }
      else {
        startBtn.disabled = true;
        stopBtn.disabled = true;
        startBtn.classList.add("secondary");
        stopBtn.classList.add("secondary");
      }
    }

    /* ---------------------------------------------------------
       DEBUG LOG
    --------------------------------------------------------- */
    function appendLog(message) {
      debugMessageCounter++;

      const node = document.createElement('div');
      node.textContent = `#${debugMessageCounter}  ${message}`;
      const log = document.getElementById('debugLog');
      log.appendChild(node);

      while (log.children.length > 200)
        log.removeChild(log.firstChild);

      document.getElementById('debugCount').textContent =
        log.children.length + ' messages';

      if (autoScroll) log.scrollTop = log.scrollHeight;
      
    }

    const debugLog = document.getElementById('debugLog');
    debugLog.addEventListener('scroll', () => {
      const atBottom =
        debugLog.scrollHeight - debugLog.scrollTop <= debugLog.clientHeight + 5;
      autoScroll = atBottom;
    });

    function clearDebugLog() {
      const log = document.getElementById('debugLog');
      log.innerHTML = '';
      debugMessageCounter = 0;
      document.getElementById('debugCount').textContent = '0 messages';
    }

    /* ---------------------------------------------------------
       WEBSOCKET
    --------------------------------------------------------- */
    function connectSocket() {
      appendLog("JS connectSocket calling new websocket with url " +
        (location.protocol === 'https:' ? 'wss' : 'ws') + '://' + location.host + '/ws');
      const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
      appendLog("location.href: " + location.href);
      appendLog("location.hostname: " + location.hostname);
      socket = new WebSocket(protocol + '://' + location.host + '/ws');
      setSocketStatus('connecting');

      socket.onopen = () => {
        appendLog("JS connectSocket onopen");
        if(!hasEverConnected)
        {          
          lastDebugId = 0;
          localStorage.setItem("lastDebugId", 0);
          hasEverConnected = true;
        }
        setSocketStatus('connected');
        socketReady = true;
        appendLog('WebSocket connected'); 
        sendMessage({command:'subscribe_status'});
        tryToSubscribe();
        /* sendMessage({command:'subscribe_debug', lastDebugId:lastDebugId}); */
      };

      socket.onclose = () => {
        setSocketStatus('disconnected');
        appendLog('WebSocket disconnected');
        socketReady = false;
        setTimeout(connectSocket, 3000);
      };

      socket.onerror = () => {
        setSocketStatus('error');
        appendLog('WebSocket error');
        socketReady = false;
      };

      socket.onmessage = event => {
        try {
          const payload = JSON.parse(event.data);
          handleServerMessage(payload);
        } catch (e) {}
      };
    }

    window.addEventListener('load', () => {
      if (!socket) connectSocket();
      // Request stored OTA server URL and current version
      setTimeout(() => {
        sendMessage({ command: 'get_ota_url' });
        sendMessage({ command: 'get_current_version' });
      }, 500);
    });

    window.onload = () => {
      domReady = true;
      tryToSubscribe();
    };

    function tryToSubscribe() {
      if (domReady && socketReady) {
        setTimeout(() => {
          sendMessage({command:'subscribe_debug', lastDebugId:lastDebugId});
        }, 600);   // 200–300 ms works reliably on iPhone
      }
    }
    function sendMessage(payload) {
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(payload));
        appendLog('TX: ' + JSON.stringify(payload));
      }
    }

          /* ---------------------------------------------------------
       SWIPE NAVIGATION (FIXED)
    --------------------------------------------------------- */
    (function() {
      const container = document.querySelector("main");
      let startX = 0;

      container.addEventListener("touchstart", e => {
        startX = e.changedTouches[0].screenX;
      }, { passive: true });

      container.addEventListener("touchend", e => {
        const endX = e.changedTouches[0].screenX;
        const diff = endX - startX;

        if (Math.abs(diff) < 50) return;

        if (diff < 0) swipePage(+1);
        else swipePage(-1);
      }, { passive: true });
    })();

    /* ---------------------------------------------------------
       SERVER MESSAGE HANDLER
    --------------------------------------------------------- */
    function handleServerMessage(payload) {
      if (payload.type === 'status') {

        const genState = payload.generator || 'UNKNOWN';
        lastKnownState = genState;
        ignoreGenState = false;

        if (pendingStop && genState === "RUNNING") ignoreGenState = true;
        if (pendingStart && genState === "NOT RUNNING") ignoreGenState = true;

        if (!ignoreGenState)
          setStatusText('generatorState', genState);

        updateGenButtons(genState);

        setSocketStatus('connected', payload.ssid || '');
        document.getElementById('acVoltage').textContent =
          payload.acVoltage || '--';
        document.getElementById('acCurrent').textContent =
          payload.acCurrent || '--';
        document.getElementById('acFrequency').textContent =
          payload.acFrequency || '--';
        if (payload.masterConnection) {
          setMasterStatus(payload.masterConnection);
        }
      }

      if (payload.type === 'debug') {
          // Save last-seen ID for reconnect sync
          lastDebugId = payload.id;
          localStorage.setItem("lastDebugId", lastDebugId);

          // Use the server ID and include the fallback message
          const msg = payload.message || 'debug event';
          //appendLog(`#${payload.id}: ${msg}`);
          appendLog(`${msg}`);
      }
      if (payload.type === 'sta_config_saved') appendLog('STA WiFi settings saved');
      if (payload.type === 'ap_config_saved') appendLog('AP WiFi settings saved');

      if (payload.type === 'STA_wifi_config')
        setSTAwifi(payload.ssid, payload.password, payload.isMaster);

      if (payload.type === 'AP_wifi_config')
        setAPwifi(payload.ssid, payload.password,
                  payload.ipAddress, payload.gateway, payload.netmask);

      if (payload.type === 'version_info') {
        document.getElementById('currentVersion').textContent = payload.current_version || '--';
        document.getElementById('availableVersion').textContent = payload.available_version || '--';
        // Show update button if available_version exists and is different
        if (payload.available_version && payload.available_version > payload.current_version) {
          document.getElementById('updateBtn').style.display = 'block';
          appendLog(`New firmware available: ${payload.available_version}`);
        } else {
          document.getElementById('updateBtn').style.display = 'none';
          appendLog('Firmware is up to date: ' + (payload.available_version || payload.current_version));
        }
      }

      if (payload.type === 'ota_url_loaded') {
        document.getElementById('updateServerUrl').value = payload.url || '';
        appendLog('Update server URL loaded');
      }

      if (payload.type === 'current_version_info') {
        document.getElementById('currentVersion').textContent = payload.version || '--';
        appendLog('Current firmware version: ' + (payload.version || '--'));
      }

      if (payload.type === 'update_progress') {
        const percentage = Math.min(100, Math.max(0, payload.progress || 0));
        document.getElementById('progressFill').style.width = percentage + '%';
        document.getElementById('updatePercentage').textContent = percentage + '%';
        // Progress debug logging is handled by C++ backend (every 20%)
        // appendLog(`Update progress: ${percentage}%`);
      }

      if (payload.type === 'update_status') {
        const statusDiv = document.getElementById('updateStatus');
        statusDiv.textContent = payload.message || 'Update status unknown';
        statusDiv.style.display = 'block';
        if (payload.status === 'success') {
          appendLog('✓ Firmware update successful!');
          document.getElementById('updateBtn').disabled = false;
          document.getElementById('updateProgress').style.display = 'none';
        } else if (payload.status === 'error') {
          appendLog('✗ Firmware update failed: ' + (payload.message || 'Unknown error'));
          document.getElementById('updateBtn').disabled = false;
          document.getElementById('updateProgress').style.display = 'none';
        }
      }
    }

    /* ---------------------------------------------------------
       UPDATED START/STOP FUNCTIONS
    --------------------------------------------------------- */
    function startGenerator() {
      const startBtn = document.getElementById("genStartButton");
      const stopBtn  = document.getElementById("genStopButton");

      pendingStart = true;
      pendingStop  = false;

      const previousState = lastKnownState;

      setButtonLoading(startBtn, "Starting...");
      stopBtn.disabled = true;
      stopBtn.classList.add("secondary");

      document.getElementById("generatorState").textContent = "STARTING";

      sendMessage({command:'start_generator'});

      startTimeout = setTimeout(() => {
        if (pendingStart) {
          pendingStart = false;
          resetButton(startBtn, "Start Gen", true);

          setStatusText("generatorState", previousState);

          stopBtn.disabled = true;
          stopBtn.classList.add("secondary");
        }
      }, 10000);
    }

    function stopGenerator() {
      const startBtn = document.getElementById("genStartButton");
      const stopBtn  = document.getElementById("genStopButton");

      pendingStop  = true;
      pendingStart = false;

      const previousState = lastKnownState;

      setButtonLoading(stopBtn, "Stopping...");
      startBtn.disabled = true;
      startBtn.classList.add("secondary");

      setStatusText("generatorState", "STOPPING");

      sendMessage({command:'stop_generator'});

      stopTimeout = setTimeout(() => {
        if (pendingStop) {
          pendingStop = false;
          resetButton(stopBtn, "Stop Gen", false);

          setStatusText("generatorState", previousState);

          startBtn.disabled = true;
          startBtn.classList.add("secondary");
        }
      }, 10000);
    }

    /* ---------------------------------------------------------
       WIFI CONFIG
    --------------------------------------------------------- */
    function setSTAwifi(ssid, password, isMaster) {
      if (ssid) document.getElementById('ssid').value = ssid;
      if (password) document.getElementById('password').value = password;
      const masterBox = document.getElementById('isMaster');
      if (masterBox) masterBox.checked = !!isMaster;
    }

    function setAPwifi(ssid, password, ip, gw, mask) {
      if (ssid) document.getElementById('ap_ssid').value = ssid;
      if (password) document.getElementById('ap_password').value = password;
      if (ip) document.getElementById('ap_ipAddress').value = ip;
      if (gw) document.getElementById('ap_gateway').value = gw;
      if (mask) document.getElementById('ap_netmask').value = mask;
    }

    function isValidIPv4(ip) {
      const parts = ip.split(".");
      if (parts.length !== 4) return false;
      return parts.every(part => {
        if (part === "" || isNaN(part)) return false;
        const n = Number(part);
        return n >= 0 && n <= 255 && String(n) === part;
      });
    }

    function saveSTAWifiConfig() {
      const ssid = document.getElementById('ssid').value;
      const password = document.getElementById('password').value;
      const isMaster = document.getElementById('isMaster').checked;
      sendMessage({command:'save_STA_wifi', ssid, password, isMaster});
      // Add blink feedback to button
      const button = event.target;
      button.classList.add('blink-feedback');
      setTimeout(() => button.classList.remove('blink-feedback'), 600);
    }

    function saveAPWifiConfig() {
      const ssid = document.getElementById('ap_ssid').value;
      const password = document.getElementById('ap_password').value;
      const ip = document.getElementById('ap_ipAddress').value;
      const gw = document.getElementById('ap_gateway').value;
      const mask = document.getElementById('ap_netmask').value;

      if (ip && !isValidIPv4(ip)) { appendLog('Invalid IP address'); return; }
      if (gw && !isValidIPv4(gw)) { appendLog('Invalid gateway'); return; }
      if (mask && !isValidIPv4(mask)) { appendLog('Invalid netmask'); return; }

      // Add blink feedback to button
      const button = event.target;
      button.classList.add('blink-feedback');
      setTimeout(() => button.classList.remove('blink-feedback'), 600);

      sendMessage({
        command:'save_AP_wifi',
        ssid, password,
        ipAddress: ip,
        gateway: gw,
        netmask: mask
      });
    }

    /* ---------------------------------------------------------
       CONFIG PAGE TABS
    --------------------------------------------------------- */
    function switchConfigTab(tab) {
      const configTab = document.getElementById('configTab');
      const updatesTab = document.getElementById('updatesTab');
      const tabBtns = document.querySelectorAll('.config-tab-btn');

      if (tab === 'config') {
        configTab.classList.add('active');
        updatesTab.classList.remove('active');
        tabBtns[0].classList.add('active');
        tabBtns[1].classList.remove('active');
      } else {
        configTab.classList.remove('active');
        updatesTab.classList.add('active');
        tabBtns[0].classList.remove('active');
        tabBtns[1].classList.add('active');
      }
    }

    /* ---------------------------------------------------------
       OTA UPDATE FUNCTIONS
    --------------------------------------------------------- */
    function checkForUpdates() {
      const updateServerUrl = document.getElementById('updateServerUrl').value;
      if (!updateServerUrl) {
        appendLog('Please enter update server URL');
        return;
      }
      // Save the URL first
      sendMessage({
        command: 'save_ota_url',
        url: updateServerUrl
      });
      // Then check for updates
      sendMessage({
        command: 'check_updates',
        update_server_url: updateServerUrl
      });
      appendLog('Checking for updates...');
    }

    function startFirmwareUpdate() {
      const updateServerUrl = document.getElementById('updateServerUrl').value;
      if (!updateServerUrl) {
        appendLog('Please enter update server URL');
        return;
      }
      // Show custom confirmation dialog instead of browser confirm()
      document.getElementById('confirmationDialog').style.display = 'flex';
    }
    
    function confirmFirmwareUpdate() {
      const updateServerUrl = document.getElementById('updateServerUrl').value;
      sendMessage({
        command: 'start_update',
        update_server_url: updateServerUrl
      });
      document.getElementById('updateProgress').style.display = 'block';
      document.getElementById('updateBtn').disabled = true;
      document.getElementById('confirmationDialog').style.display = 'none';
      appendLog('Starting firmware update...');
    }
    
    function cancelFirmwareUpdate() {
      document.getElementById('confirmationDialog').style.display = 'none';
      appendLog('Firmware update cancelled');
    }

  </script>

</body>
</html>
)HTML";

#endif // SLAVE_PAGES_H