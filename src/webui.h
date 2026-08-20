/*
 * webui.h — Web configuration UI
 * v1.1.0
 *
 * Routes:
 *   GET  /          — config form + manual test controller + live DMX monitor
 *   POST /save      — save config, restart
 *   GET  /status    — JSON status
 *   GET  /dmx       — JSON dmx buffer (512 values)
 *   GET  /set       — set one DMX channel (?ch=1-512&val=0-255), manual test only
 *   POST /update    — OTA firmware upload
 */
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include "config.h"

static const char WEBUI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ArtDMX Node</title>
<style>
  :root{--bg:#111;--card:#1a1a1a;--accent:#e67e22;--text:#eee;--muted:#888;--border:#333}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--text);font-family:system-ui,sans-serif;padding:20px}
  h1{color:var(--accent);margin-bottom:4px;font-size:1.4rem}
  .sub{color:var(--muted);font-size:.8rem;margin-bottom:20px}
  .card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:16px;margin-bottom:16px}
  .card h2{font-size:.9rem;color:var(--accent);margin-bottom:12px;text-transform:uppercase;letter-spacing:.05em}
  label{display:block;font-size:.8rem;color:var(--muted);margin-bottom:3px}
  input,select{width:100%;background:#222;border:1px solid var(--border);color:var(--text);padding:7px 10px;border-radius:5px;font-size:.9rem;margin-bottom:12px}
  .row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
  button{background:var(--accent);color:#fff;border:none;padding:9px 20px;border-radius:5px;font-size:.9rem;cursor:pointer}
  button:hover{opacity:.85}
  .btn-ota{background:#444;margin-left:8px}
  #status{display:flex;gap:16px;flex-wrap:wrap;margin-bottom:16px}
  .stat{background:var(--card);border:1px solid var(--border);border-radius:6px;padding:10px 14px;font-size:.8rem}
  .stat strong{display:block;font-size:1.2rem;color:var(--accent)}
  /* DMX grid */
  #dmxGrid{display:grid;grid-template-columns:repeat(16,1fr);gap:2px;font-size:.6rem}
  .dc{background:#222;border-radius:2px;padding:2px;text-align:center;transition:background .1s}
  .dc span{display:block;color:var(--muted);font-size:.5rem}
  #otaStatus{margin-top:8px;font-size:.8rem;color:var(--accent)}
  /* Manual test controller — skeuomorphic fader bank */
  #sliderGrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(58px,1fr));gap:10px;margin-top:12px}
  .fader{
    background:linear-gradient(180deg,#333336,#19191b 60%,#0f0f10);
    border:1px solid #000;
    border-radius:8px;
    padding:10px 4px 12px;
    display:flex;
    flex-direction:column;
    align-items:center;
    box-shadow:inset 0 1px 0 rgba(255,255,255,.07), 0 2px 5px rgba(0,0,0,.5);
  }
  .fader label{
    color:#ddd;
    font-size:.6rem;
    font-weight:700;
    letter-spacing:.03em;
    margin-bottom:8px;
    background:#000;
    border:1px solid #3a3a3a;
    border-radius:3px;
    padding:2px 6px;
    min-width:26px;
    text-align:center;
  }
  .fader .track{
    position:relative;
    width:32px;
    height:140px;
  }
  .fader .track::before{
    content:'';
    position:absolute;
    left:50%; top:0;
    width:6px; height:100%;
    transform:translateX(-50%);
    background:linear-gradient(90deg,#020202,#222 45%,#020202);
    border-radius:3px;
    box-shadow:inset 0 0 4px #000, inset 0 0 1px rgba(255,255,255,.1);
  }
  .fader .fill{
    position:absolute;
    left:50%; bottom:0;
    width:6px; height:0%;
    transform:translateX(-50%);
    background:linear-gradient(180deg,#ffcf8a,var(--accent) 70%,#b3560f);
    border-radius:3px 3px 1px 1px;
    box-shadow:0 0 6px rgba(230,126,34,.65);
    pointer-events:none;
    transition:height .06s linear;
  }
  .fader input[type=range]{
    -webkit-appearance:none;
    appearance:none;
    writing-mode:vertical-lr;
    direction:rtl;
    width:32px;
    height:140px;
    margin:0;
    background:transparent;
    position:relative;
    display:block;
  }
  .fader input[type=range]:focus{outline:none}
  .fader input[type=range]::-webkit-slider-runnable-track{width:32px;background:transparent}
  .fader input[type=range]::-moz-range-track{width:32px;background:transparent;border:none}
  .fader input[type=range]::-webkit-slider-thumb{
    -webkit-appearance:none;
    width:32px; height:30px;
    border-radius:4px;
    background:
      radial-gradient(ellipse 55% 24% at 50% 44%, rgba(255,255,255,.22), rgba(255,255,255,0) 62%),
      radial-gradient(ellipse 55% 28% at 50% 56%, rgba(0,0,0,.55), rgba(0,0,0,0) 65%),
      linear-gradient(180deg, rgba(255,255,255,.14), rgba(255,255,255,0) 18%),
      linear-gradient(180deg,#6a6a6e,#333336 50%,#141416);
    border:1px solid #000;
    box-shadow:0 1px 0 rgba(255,255,255,.2) inset, 0 2px 3px rgba(0,0,0,.7);
    cursor:grab;
  }
  .fader input[type=range]::-moz-range-thumb{
    width:32px; height:30px;
    border-radius:4px;
    background:
      radial-gradient(ellipse 55% 24% at 50% 44%, rgba(255,255,255,.22), rgba(255,255,255,0) 62%),
      radial-gradient(ellipse 55% 28% at 50% 56%, rgba(0,0,0,.55), rgba(0,0,0,0) 65%),
      linear-gradient(180deg, rgba(255,255,255,.14), rgba(255,255,255,0) 18%),
      linear-gradient(180deg,#6a6a6e,#333336 50%,#141416);
    border:1px solid #000;
    box-shadow:0 1px 0 rgba(255,255,255,.2) inset, 0 2px 3px rgba(0,0,0,.7);
    cursor:grab;
  }
  .fader input[type=range]:active::-webkit-slider-thumb{cursor:grabbing}
  .fader input[type=range]:active::-moz-range-thumb{cursor:grabbing}
  .fader .fv{
    margin-top:8px;
    font-family:'Courier New',monospace;
    font-size:.72rem;
    font-weight:700;
    color:#ffb347;
    background:#000;
    border:1px solid #3a3a3a;
    border-radius:3px;
    padding:2px 6px;
    min-width:28px;
    text-align:center;
    text-shadow:0 0 4px rgba(255,179,71,.65);
  }
  .testBtns{display:flex;gap:8px;margin-top:12px}
  .testBtns button{flex:1;padding:7px 0}
  .btn-dim{background:#444}
</style>
</head>
<body>
<h1>&#9679; ArtDMX Node</h1>
<div class="sub" id="ver">v1.0.0 &mdash; <span id="ip">-</span></div>

<div id="status">
  <div class="stat"><span>Source</span><strong id="sSrc">-</strong></div>
  <div class="stat"><span>Universe</span><strong id="sUni">-</strong></div>
  <div class="stat"><span>Packets/s</span><strong id="sPps">0</strong></div>
  <div class="stat"><span>DMX fps</span><strong id="sFps">0</strong></div>
</div>

<div class="card">
  <h2>Network</h2>
  <div class="row">
    <div><label>SSID</label><input id="ssid" name="ssid" maxlength="63"></div>
    <div><label>Password</label><input id="pass" name="pass" type="password" maxlength="63"></div>
  </div>
  <div class="row">
    <div><label>Hostname</label><input id="hostname" name="hostname" maxlength="31"></div>
    <div><label>OTA Password</label><input id="otapwd" name="otapwd" type="password" maxlength="31"></div>
  </div>
  <label>IP Mode</label>
  <select id="dhcp" name="dhcp" onchange="toggleStatic()">
    <option value="1">DHCP</option>
    <option value="0">Static</option>
  </select>
  <div id="staticFields" style="display:none">
    <div class="row">
      <div><label>Static IP</label><input id="staticip" name="staticip"></div>
      <div><label>Gateway</label><input id="gateway" name="gateway"></div>
    </div>
    <div><label>Subnet</label><input id="subnet" name="subnet"></div>
  </div>
</div>

<div class="card">
  <h2>Art-Net / sACN</h2>
  <div class="row">
    <div><label>Universe (0-32767)</label><input id="universe" name="universe" type="number" min="0" max="32767"></div>
    <div><label>sACN Priority (0-200)</label><input id="sacnpri" name="sacnpri" type="number" min="0" max="200"></div>
  </div>
</div>

<div class="card">
  <h2>RDM</h2>
  <div class="row">
    <div><label>Device Label</label><input id="rdmlabel" name="rdmlabel" maxlength="32"></div>
    <div><label>DMX Start Address</label><input id="dmxstart" name="dmxstart" type="number" min="1" max="512"></div>
  </div>
</div>

<button onclick="saveConfig()">Save &amp; Restart</button>

<div class="card" style="margin-top:16px">
  <h2>Manual DMX Test Controller</h2>
  <div class="row">
    <div><label>Start Address (1-512)</label><input id="testStart" type="number" min="1" max="512" value="1"></div>
    <div><label>Channel Count (1-32)</label><input id="testCount" type="number" min="1" max="32" value="16"></div>
  </div>
  <button onclick="buildFaders()">Apply</button>
  <div class="testBtns">
    <button class="btn-dim" onclick="setAllFaders(0)">Blackout</button>
    <button class="btn-dim" onclick="setAllFaders(255)">Full</button>
  </div>
  <div id="sliderGrid"></div>
</div>

<div class="card">
  <h2>Live DMX Monitor</h2>
  <div id="dmxGrid"></div>
</div>

<div class="card">
  <h2>OTA Firmware Update</h2>
  <input type="file" id="otaFile" accept=".bin" style="width:auto;margin-bottom:8px">
  <button onclick="uploadFirmware()">Upload</button>
  <div id="otaStatus"></div>
</div>

<script>
let cfg = {};

async function loadConfig() {
  const r = await fetch('/status');
  cfg = await r.json();
  document.getElementById('ssid').value     = cfg.ssid     || '';
  document.getElementById('hostname').value = cfg.hostname || '';
  document.getElementById('universe').value = cfg.universe ?? 0;
  document.getElementById('sacnpri').value  = cfg.sacnpri  ?? 100;
  document.getElementById('dhcp').value     = cfg.dhcp ? '1' : '0';
  document.getElementById('staticip').value = cfg.staticip || '';
  document.getElementById('gateway').value  = cfg.gateway  || '';
  document.getElementById('subnet').value   = cfg.subnet   || '';
  document.getElementById('rdmlabel').value = cfg.rdmlabel || '';
  document.getElementById('dmxstart').value = cfg.dmxstart || 1;
  document.getElementById('ip').textContent = cfg.ip || '-';
  document.getElementById('sUni').textContent = cfg.universe ?? '-';
  document.getElementById('sSrc').textContent = cfg.lastSrc || '-';
  toggleStatic();
}

function toggleStatic() {
  document.getElementById('staticFields').style.display =
    document.getElementById('dhcp').value === '0' ? 'block' : 'none';
}

async function saveConfig() {
  const body = new URLSearchParams({
    ssid:     document.getElementById('ssid').value,
    pass:     document.getElementById('pass').value,
    otapwd:   document.getElementById('otapwd').value,
    hostname: document.getElementById('hostname').value,
    universe: document.getElementById('universe').value,
    sacnpri:  document.getElementById('sacnpri').value,
    dhcp:     document.getElementById('dhcp').value,
    staticip: document.getElementById('staticip').value,
    gateway:  document.getElementById('gateway').value,
    subnet:   document.getElementById('subnet').value,
    rdmlabel: document.getElementById('rdmlabel').value,
    dmxstart: document.getElementById('dmxstart').value,
  });
  await fetch('/save', {method:'POST', body});
  alert('Saved — restarting...');
}

// ─── Manual DMX test controller ───────────────────────────────────────────
let sendTimers = {};

function sendChannel(ch, val) {
  clearTimeout(sendTimers[ch]);
  sendTimers[ch] = setTimeout(() => {
    fetch(`/set?ch=${ch}&val=${val}`);
  }, 25);
}

function onFaderInput(el) {
  const ch = el.dataset.ch;
  const val = el.value;
  const fader = el.closest('.fader');
  fader.querySelector('.fv').textContent = val;
  fader.querySelector('.fill').style.height = (val / 255 * 100) + '%';
  sendChannel(ch, val);
}

function buildFaders() {
  const start = Math.min(512, Math.max(1, parseInt(document.getElementById('testStart').value) || 1));
  const count = Math.min(32, Math.max(1, parseInt(document.getElementById('testCount').value) || 16));
  const grid = document.getElementById('sliderGrid');
  grid.innerHTML = '';
  for (let i = 0; i < count; i++) {
    const ch = start + i;
    if (ch > 512) break;
    const f = document.createElement('div');
    f.className = 'fader';
    f.innerHTML = `<label>CH ${ch}</label>
      <div class="track">
        <div class="fill"></div>
        <input type="range" min="0" max="255" value="0" data-ch="${ch}" oninput="onFaderInput(this)">
      </div>
      <span class="fv">0</span>`;
    grid.appendChild(f);
  }
}

function setAllFaders(val) {
  document.querySelectorAll('#sliderGrid input[type=range]').forEach(el => {
    el.value = val;
    onFaderInput(el);
  });
}

// Build DMX grid
const grid = document.getElementById('dmxGrid');
for (let i = 1; i <= 512; i++) {
  const d = document.createElement('div');
  d.className = 'dc';
  d.id = 'c' + i;
  d.innerHTML = `<span>${i}</span><span class="v">0</span>`;
  grid.appendChild(d);
}

async function pollDmx() {
  try {
    const r = await fetch('/dmx');
    const data = await r.json();
    for (let i = 1; i <= 512; i++) {
      const v = data[i] || 0;
      const el = document.getElementById('c' + i);
      if (el) {
        el.querySelector('.v').textContent = v;
        const bright = Math.round(v / 255 * 40);
        el.style.background = `hsl(30,80%,${8 + bright}%)`;
      }
    }
    document.getElementById('sFps').textContent = data.fps || 0;
    document.getElementById('sPps').textContent = data.pps || 0;
    document.getElementById('sSrc').textContent = data.src || '-';
  } catch(e){}
}

async function uploadFirmware() {
  const file = document.getElementById('otaFile').files[0];
  if (!file) return alert('Select a .bin file');
  const st = document.getElementById('otaStatus');
  st.textContent = 'Uploading...';
  const fd = new FormData();
  fd.append('firmware', file);
  const r = await fetch('/update', {method:'POST', body: fd});
  st.textContent = r.ok ? 'Done — rebooting...' : 'Upload failed: ' + await r.text();
}

loadConfig();
buildFaders();
setInterval(pollDmx, 100);
</script>
</body>
</html>
)rawhtml";

// ─── Metrics ─────────────────────────────────────────────────────────────────
static uint32_t pktCount    = 0;
static uint32_t dmxFrames   = 0;
static uint32_t lastMetric  = 0;
static float    pps = 0, fps = 0;
static char     lastSrc[8]  = "-";

inline void webuiCountPacket(const char* src) {
  pktCount++;
  strlcpy(lastSrc, src, sizeof(lastSrc));
}
inline void webuiCountDmxFrame() { dmxFrames++; }

inline void webuiUpdateMetrics() {
  uint32_t now = millis();
  if (now - lastMetric >= 1000) {
    pps = pktCount;
    fps = dmxFrames;
    pktCount = dmxFrames = 0;
    lastMetric = now;
  }
}

// ─── Setup routes ─────────────────────────────────────────────────────────────
inline void webuiSetup(WebServer& server, Config& cfg, Preferences& prefs,
                        uint8_t* dmxBuffer, volatile bool* dmxDirty) {
  server.on("/", HTTP_GET, [&server]() {
    server.send_P(200, "text/html", WEBUI_HTML);
  });

  // Manual DMX test controller — set one channel (bypasses Art-Net/sACN
  // until the next network packet overwrites it)
  // dmxBuffer captured by value (it's a pointer to the caller's persistent
  // global buffer) — capturing it by reference here would bind to this
  // function's stack parameter, which is gone once webuiSetup() returns,
  // long before these handlers ever actually run.
  server.on("/set", HTTP_GET, [&server, dmxBuffer, dmxDirty]() {
    if (server.hasArg("ch") && server.hasArg("val")) {
      int ch  = server.arg("ch").toInt();
      int val = server.arg("val").toInt();
      if (ch >= 1 && ch <= 512) {
        dmxBuffer[ch] = (uint8_t)constrain(val, 0, 255);
        *dmxDirty = true;
      }
    }
    server.send(200, "text/plain", "ok");
  });

  server.on("/status", HTTP_GET, [&server, &cfg]() {
    webuiUpdateMetrics();
    char json[512];
    snprintf(json, sizeof(json),
      "{\"ssid\":\"%s\",\"hostname\":\"%s\",\"universe\":%d,"
      "\"sacnpri\":%d,\"dhcp\":%s,"
      "\"staticip\":\"%s\",\"gateway\":\"%s\",\"subnet\":\"%s\","
      "\"rdmlabel\":\"%s\",\"dmxstart\":%d,"
      "\"ip\":\"%s\",\"lastSrc\":\"%s\",\"pps\":%.0f,\"fps\":%.0f}",
      cfg.ssid, cfg.hostname, cfg.universe,
      cfg.sacnPriority, cfg.dhcp ? "true" : "false",
      cfg.staticIP.toString().c_str(),
      cfg.gateway.toString().c_str(),
      cfg.subnet.toString().c_str(),
      cfg.rdmLabel, cfg.dmxStartAddress,
      WiFi.localIP().toString().c_str(),
      lastSrc, pps, fps);
    server.send(200, "application/json", json);
  });

  server.on("/dmx", HTTP_GET, [&server, dmxBuffer]() {
    webuiUpdateMetrics();
    // Static buffer — no heap allocation on this hot path (polled every
    // 100ms by the browser). Repeated String concatenation here fragmented
    // the heap until a realloc failed and crashed the AP.
    static char buf[6144];
    int pos = snprintf(buf, sizeof(buf), "{");
    for (int i = 1; i <= 512 && pos < (int)sizeof(buf); i++) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "\"%d\":%u%s",
                       i, dmxBuffer[i], (i < 512) ? "," : "");
    }
    if (pos < (int)sizeof(buf)) {
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                       ",\"fps\":%d,\"pps\":%d,\"src\":\"%s\"}",
                       (int)fps, (int)pps, lastSrc);
    }
    server.send(200, "application/json", buf);
  });

  server.on("/save", HTTP_POST, [&server, &cfg, &prefs]() {
    if (server.hasArg("ssid"))     strlcpy(cfg.ssid,     server.arg("ssid").c_str(),     sizeof(cfg.ssid));
    if (server.hasArg("pass") && server.arg("pass").length())
                                   strlcpy(cfg.password,  server.arg("pass").c_str(),     sizeof(cfg.password));
    if (server.hasArg("otapwd") && server.arg("otapwd").length())
                                   strlcpy(cfg.otaPassword, server.arg("otapwd").c_str(), sizeof(cfg.otaPassword));
    if (server.hasArg("hostname")) strlcpy(cfg.hostname, server.arg("hostname").c_str(), sizeof(cfg.hostname));
    if (server.hasArg("rdmlabel")) strlcpy(cfg.rdmLabel, server.arg("rdmlabel").c_str(), sizeof(cfg.rdmLabel));
    if (server.hasArg("universe"))  cfg.universe        = server.arg("universe").toInt();
    if (server.hasArg("sacnpri"))   cfg.sacnPriority    = server.arg("sacnpri").toInt();
    if (server.hasArg("dmxstart"))  cfg.dmxStartAddress = server.arg("dmxstart").toInt();
    if (server.hasArg("dhcp"))      cfg.dhcp            = server.arg("dhcp") == "1";
    if (server.hasArg("staticip"))  cfg.staticIP.fromString(server.arg("staticip"));
    if (server.hasArg("gateway"))   cfg.gateway.fromString(server.arg("gateway"));
    if (server.hasArg("subnet"))    cfg.subnet.fromString(server.arg("subnet"));
    cfg.save(prefs);
    server.send(200, "text/plain", "ok");
    delay(500);
    ESP.restart();
  });

  // OTA web upload
  server.on("/update", HTTP_POST,
    [&server]() {
      server.send(Update.hasError() ? 500 : 200, "text/plain",
                  Update.hasError() ? "FAIL" : "OK");
      delay(500);
      ESP.restart();
    },
    [&server]() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[ota-web] start: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[ota-web] done: %u bytes\n", upload.totalSize);
        else Update.printError(Serial);
      }
    }
  );
}
