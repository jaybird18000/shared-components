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
    header { background:#132a45; padding:18px; text-align:center; box-shadow:0 2px 12px rgba(0,0,0,0.4); }
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
  </style>
</head>

<body>
  <header>
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

        <div class="card">
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
        setSTAwifi(payload.ssid, payload.password);

      if (payload.type === 'AP_wifi_config')
        setAPwifi(payload.ssid, payload.password,
                  payload.ipAddress, payload.gateway, payload.netmask);
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
    function setSTAwifi(ssid, password) {
      if (ssid) document.getElementById('ssid').value = ssid;
      if (password) document.getElementById('password').value = password;
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
      sendMessage({command:'save_STA_wifi', ssid, password});
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

      sendMessage({
        command:'save_AP_wifi',
        ssid, password,
        ipAddress: ip,
        gateway: gw,
        netmask: mask
      });
    }

  </script>

</body>
</html>
)HTML";

#endif // SLAVE_PAGES_H