#!/usr/bin/env python3
"""
Art-Net web controller — sends real Art-Net (ArtDmx) UDP packets to an
Art-Net node (e.g. the ArtDMX ESP32 node) over WiFi.

Browsers can't send raw UDP from JavaScript, so this is a small local
HTTP server: it serves the slider UI and does the actual Art-Net send
on the browser's behalf.

Usage:
    python3 server.py --target-ip 192.168.1.200 --universe 0

Then open http://localhost:8000 in a browser on the same network.
"""
import argparse
import json
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ARTNET_ID = b"Art-Net\x00"
OP_DMX = 0x5000

dmx_buffer = bytearray(512)
buffer_lock = threading.Lock()
sequence = 0
send_event = threading.Event()


def build_artdmx_packet(universe: int, data: bytes) -> bytes:
    global sequence
    sequence = (sequence % 255) + 1  # 1-255, 0 means "sequencing disabled"
    packet = bytearray()
    packet += ARTNET_ID
    packet += OP_DMX.to_bytes(2, "little")
    packet += (14).to_bytes(2, "big")           # ProtVer
    packet += bytes([sequence])
    packet += bytes([0])                         # Physical
    packet += bytes([universe & 0xFF])            # SubUni
    packet += bytes([(universe >> 8) & 0x7F])     # Net
    packet += len(data).to_bytes(2, "big")        # Length
    packet += data
    return bytes(packet)


def send_artnet(server):
    with buffer_lock:
        data = bytes(dmx_buffer)
    packet = build_artdmx_packet(server.universe, data)
    server.sock.sendto(packet, (server.target_ip, server.artnet_port))


def sender_loop(server, keepalive_interval=1.0):
    # Single dedicated sender thread — this is the ONLY thread that ever
    # calls sock.sendto(). HTTP handler threads just update dmx_buffer and
    # set send_event; they never send directly. Multiple handler threads
    # racing to send their own snapshots could deliver an older buffer
    # state *after* a newer one (thread scheduling / UDP has no ordering
    # guarantee), and the ESP32 parser has no sequence checking, so a
    # stale packet arriving last would silently win and "stick" a channel
    # at an old value. Serializing all sends here removes that race.
    # Target IP/universe are read fresh from `server` every send (rather
    # than fixed at thread-start) so /target can change them live.
    while True:
        send_event.wait(timeout=keepalive_interval)
        send_event.clear()
        try:
            send_artnet(server)
        except OSError as e:
            # An unhandled exception here would silently kill this thread
            # forever (e.g. "Network is unreachable" when the Mac switches
            # WiFi networks and the long-lived socket's cached route goes
            # stale — confirmed: a freshly-created socket sent fine
            # immediately after this exact failure). Recreate the socket
            # so it self-heals on the next send instead of staying dead
            # until the process is restarted.
            print(f"[sender] send failed: {e} — recreating socket")
            try:
                server.sock.close()
            except OSError:
                pass
            server.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Art-Net Controller</title>
<style>
  :root{--bg:#111;--card:#1a1a1a;--accent:#e67e22;--text:#eee;--muted:#888;--border:#333}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--text);font-family:system-ui,sans-serif;padding:20px}
  h1{color:var(--accent);margin-bottom:4px;font-size:1.4rem}
  .sub{color:var(--muted);font-size:.8rem;margin-bottom:20px}
  .card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:16px;margin-bottom:16px}
  .card h2{font-size:.9rem;color:var(--accent);margin-bottom:12px;text-transform:uppercase;letter-spacing:.05em}
  label{display:block;font-size:.8rem;color:var(--muted);margin-bottom:3px}
  input{width:100%;background:#222;border:1px solid var(--border);color:var(--text);padding:7px 10px;border-radius:5px;font-size:.9rem;margin-bottom:12px}
  .row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
  button{background:var(--accent);color:#fff;border:none;padding:9px 20px;border-radius:5px;font-size:.9rem;cursor:pointer}
  button:hover{opacity:.85}
  .btn-dim{background:#444}
  .testBtns{display:flex;gap:8px;margin-bottom:16px}
  .testBtns button{flex:1;padding:7px 0}
  .stat{background:var(--card);border:1px solid var(--border);border-radius:6px;padding:10px 14px;font-size:.8rem}
  .stat strong{display:block;font-size:1.1rem;color:var(--accent)}

  /* Console-strip fader bank */
  #sliderGrid{display:flex;gap:6px;flex-wrap:wrap}
  .strip{
    width:94px;
    background:linear-gradient(180deg,#9a9a9e,#828288 40%,#75757a);
    border:1px solid #4a4a4e;
    border-radius:8px;
    padding:6px 6px 10px;
    display:flex;
    flex-direction:column;
    align-items:center;
    box-shadow:0 3px 6px rgba(0,0,0,.5), inset 0 1px 0 rgba(255,255,255,.25);
    position:relative;
  }
  .strip::before, .strip::after{
    content:'';
    position:absolute;
    width:5px; height:5px;
    border-radius:50%;
    background:radial-gradient(circle at 35% 35%, #444, #111);
    top:5px;
  }
  .strip::before{left:6px}
  .strip::after{right:6px}
  .strip .tab{
    width:100%;
    border-radius:4px 4px 0 0;
    padding:3px 0;
    text-align:center;
    font-size:.62rem;
    font-weight:700;
    color:#fff;
    text-shadow:0 1px 1px rgba(0,0,0,.4);
    letter-spacing:.02em;
  }
  .strip .screen{
    width:100%;
    background:#0a0a0a;
    border:1px solid #000;
    border-top:none;
    padding:4px 6px;
    margin-bottom:8px;
    font-family:'Courier New',monospace;
  }
  .screen .row{display:flex;justify-content:space-between;align-items:center;font-size:.55rem;color:#7a7a7a;line-height:1.5}
  .screen .row b{color:#e8e8e8;font-size:.62rem;font-weight:700}
  .screen .meter{width:100%;height:4px;background:#1c1c1c;border-radius:2px;margin-top:3px;overflow:hidden}
  .screen .meter i{display:block;height:100%;width:0%;background:linear-gradient(90deg,#3fae4a,#d9c73f 70%,#d94f4f);transition:width .05s linear}
  .knobrow{display:flex;gap:8px;margin-bottom:10px}
  .knob{
    width:15px; height:15px;
    border-radius:50%;
    background:radial-gradient(circle at 35% 30%, #4a4a4c, #1c1c1e 70%);
    border:1px solid #0a0a0a;
    box-shadow:0 1px 2px rgba(0,0,0,.6), inset 0 0 2px rgba(255,255,255,.15);
    position:relative;
  }
  .knob::after{
    content:'';
    position:absolute;
    top:2px; left:50%;
    width:1.5px; height:5px;
    background:#c9c9c9;
    transform:translateX(-50%);
    border-radius:1px;
  }
  .strip .track{position:relative;width:34px;height:130px;margin-bottom:8px}
  .strip .track::before{
    content:'';
    position:absolute;
    left:50%; top:0;
    width:6px; height:100%;
    transform:translateX(-50%);
    background:linear-gradient(90deg,#0a0a0a,#2a2a2a 45%,#0a0a0a);
    border-radius:3px;
    box-shadow:inset 0 0 4px #000;
  }
  .strip .fill{
    position:absolute;
    left:50%; bottom:0;
    width:6px; height:0%;
    transform:translateX(-50%);
    background:linear-gradient(180deg,#ffcf8a,var(--accent) 70%,#b3560f);
    border-radius:3px 3px 1px 1px;
    box-shadow:0 0 6px rgba(230,126,34,.6);
    pointer-events:none;
    transition:height .06s linear;
  }
  .strip input[type=range]{
    -webkit-appearance:none;
    appearance:none;
    writing-mode:vertical-lr;
    direction:rtl;
    width:34px;
    height:130px;
    margin:0;
    background:transparent;
    display:block;
  }
  .strip input[type=range]:focus{outline:none}
  .strip input[type=range]::-webkit-slider-runnable-track{width:34px;background:transparent}
  .strip input[type=range]::-moz-range-track{width:34px;background:transparent;border:none}
  .strip input[type=range]::-webkit-slider-thumb{
    -webkit-appearance:none;
    width:34px; height:34px;
    border-radius:4px;
    background:
      repeating-linear-gradient(0deg, rgba(0,0,0,.35) 0 1.5px, rgba(255,255,255,.12) 1.5px 3px, transparent 3px 5px),
      linear-gradient(180deg,#d8d8dc,#a8a8ae 45%,#828288);
    border:1px solid #3a3a3e;
    box-shadow:0 1px 0 rgba(255,255,255,.4) inset, 0 2px 4px rgba(0,0,0,.6);
    cursor:grab;
  }
  .strip input[type=range]::-moz-range-thumb{
    width:34px; height:34px;
    border-radius:4px;
    background:
      repeating-linear-gradient(0deg, rgba(0,0,0,.35) 0 1.5px, rgba(255,255,255,.12) 1.5px 3px, transparent 3px 5px),
      linear-gradient(180deg,#d8d8dc,#a8a8ae 45%,#828288);
    border:1px solid #3a3a3e;
    box-shadow:0 1px 0 rgba(255,255,255,.4) inset, 0 2px 4px rgba(0,0,0,.6);
    cursor:grab;
  }
  .btnrow{display:flex;gap:6px;margin-bottom:8px}
  .dot{
    width:10px; height:10px;
    border-radius:50%;
    background:radial-gradient(circle at 35% 30%, #333, #111);
    border:1px solid #000;
    box-shadow:inset 0 0 2px rgba(0,0,0,.8);
  }
  .dot.on{background:radial-gradient(circle at 35% 30%, #ffd27a, var(--accent));box-shadow:0 0 6px rgba(230,126,34,.8)}
  .mutepill{
    flex:1;
    height:10px;
    border-radius:3px;
    background:#2a2a2a;
    border:1px solid #111;
  }
  .strip .chnum{
    font-size:.6rem;
    font-weight:700;
    color:#2a2a2a;
    background:#c4c4c8;
    border:1px solid #6a6a6e;
    border-radius:3px;
    padding:1px 7px;
    margin-top:2px;
  }

  /* Color wheel */
  #wheelWrap{position:relative;width:180px;height:180px;margin:8px auto 16px;touch-action:none}
  #hueWheel{border-radius:50%;box-shadow:0 2px 8px rgba(0,0,0,.6);cursor:crosshair;-webkit-user-drag:none;user-select:none}
  #wheelCursor{
    position:absolute;width:14px;height:14px;border-radius:50%;
    border:2px solid #fff;box-shadow:0 0 4px rgba(0,0,0,.8);
    transform:translate(-50%,-50%);pointer-events:none;
    left:90px;top:0px;
  }
  .colorPresets{display:grid;grid-template-columns:repeat(8,1fr);gap:6px;margin-top:12px}
  .swatch{
    width:100%;aspect-ratio:1;border-radius:5px;border:1px solid rgba(255,255,255,.2);
    cursor:pointer;padding:0
  }
  .swatch:hover{opacity:.85}
  .effectBtn{position:relative;flex-shrink:0;width:90px}
  .effectBtn.active{background:#2fb8b0}
  .effectRow{display:flex;align-items:center;gap:10px;margin-bottom:10px}
  .speedSlider{flex:1;margin-bottom:0}
</style>
</head>
<body>
<h1>&#9679; Art-Net Controller</h1>
<div class="sub">sends real Art-Net (ArtDmx) packets over WiFi</div>

<div class="card">
  <h2>Target</h2>
  <div class="row">
    <div><label>Node IP</label><input id="targetIp" value="__TARGET_IP__"></div>
    <div><label>Universe</label><input id="targetUniverse" type="number" min="0" max="32767" value="__UNIVERSE__"></div>
  </div>
  <button onclick="applyTarget()">Apply</button>
  <div id="targetStatus" style="margin-top:8px;font-size:.8rem;color:var(--muted)"></div>
</div>

<div class="card">
  <h2>Range</h2>
  <div class="row">
    <div><label>Start Address (1-512)</label><input id="testStart" type="number" min="1" max="512" value="1"></div>
    <div><label>Channel Count (1-16)</label><input id="testCount" type="number" min="1" max="16" value="10"></div>
  </div>
  <button onclick="buildFaders()">Apply</button>
</div>

<div class="card">
  <h2>Color</h2>
  <div class="row">
    <div><label>Red Ch</label><input id="chR" type="number" min="1" max="512" value="1"></div>
    <div><label>Green Ch</label><input id="chG" type="number" min="1" max="512" value="2"></div>
  </div>
  <div class="row">
    <div><label>Blue Ch</label><input id="chB" type="number" min="1" max="512" value="3"></div>
    <div><label>Dimmer Ch (0 = none)</label><input id="chDim" type="number" min="0" max="512" value="0"></div>
  </div>
  <div id="wheelWrap">
    <canvas id="hueWheel" width="180" height="180"></canvas>
    <div id="wheelCursor"></div>
  </div>
  <label>Brightness</label>
  <input id="brightness" type="range" min="0" max="255" value="255" oninput="onBrightnessInput()">
  <div class="colorPresets" id="colorPresets"></div>
</div>

<div class="card">
  <h2>Effects</h2>
  <div class="effectRow">
    <button class="effectBtn" id="btnRainbow" onclick="toggleEffect('rainbow')">Rainbow</button>
    <input type="range" class="speedSlider" id="speedRainbow" min="15" max="300" value="60" oninput="onSpeedChange('rainbow')">
  </div>
  <div class="effectRow">
    <button class="effectBtn" id="btnStrobe" onclick="toggleEffect('strobe')">Strobe</button>
    <input type="range" class="speedSlider" id="speedStrobe" min="30" max="600" value="100" oninput="onSpeedChange('strobe')">
  </div>
  <div class="effectRow">
    <button class="effectBtn" id="btnPulse" onclick="toggleEffect('pulse')">Pulse</button>
    <input type="range" class="speedSlider" id="speedPulse" min="10" max="150" value="40" oninput="onSpeedChange('pulse')">
  </div>
  <div class="effectRow">
    <button class="effectBtn" id="btnChase" onclick="toggleEffect('chase')">Chase</button>
    <input type="range" class="speedSlider" id="speedChase" min="60" max="1000" value="300" oninput="onSpeedChange('chase')">
  </div>
  <button class="btn-dim" onclick="stopEffects()" style="width:100%;margin-top:4px">Stop</button>
</div>

<div class="testBtns">
  <button class="btn-dim" onclick="setAllFaders(0)">Blackout</button>
  <button class="btn-dim" onclick="setAllFaders(255)">Full</button>
</div>

<div id="sliderGrid"></div>

<script>
const palette = ['#3b82c4','#d94f4f','#d4b83c','#4a9d5f','#8b5cf6','#2fb8b0','#d946a8','#e07b39','#3fb8c9','#b8860b'];
let sendTimers = {};

async function applyTarget() {
  const ip = document.getElementById('targetIp').value.trim();
  const universe = document.getElementById('targetUniverse').value;
  const status = document.getElementById('targetStatus');
  status.textContent = 'Applying...';
  const r = await fetch(`/target?ip=${encodeURIComponent(ip)}&universe=${universe}`);
  status.textContent = r.ok ? `Sending to ${ip}, universe ${universe}` : 'Invalid IP or universe';
}

function sendChannel(ch, val) {
  clearTimeout(sendTimers[ch]);
  sendTimers[ch] = setTimeout(() => { fetch(`/set?ch=${ch}&val=${val}`); }, 25);
}

function updateFaderVisual(ch, val){
  const el = document.querySelector(`#sliderGrid input[data-ch="${ch}"]`);
  if (!el) return;
  el.value = val;
  const strip = el.closest('.strip');
  strip.querySelector('.val').innerHTML = val;
  strip.querySelector('.meter i').style.width = (val/255*100)+'%';
  strip.querySelector('.fill').style.height = (val/255*100)+'%';
  strip.querySelector('.dot').classList.toggle('on', val>0);
}

function onFaderInput(el){
  const ch = el.dataset.ch;
  const val = +el.value;
  updateFaderVisual(ch, val);
  sendChannel(ch, val);
}

// ─── Color wheel ───────────────────────────────────────────────────────────
let currentHue = 0, currentSat = 0;

function hsvToRgb(h, s, v){
  const c = v * s;
  const x = c * (1 - Math.abs((h / 60) % 2 - 1));
  const m = v - c;
  let r,g,b;
  if      (h <  60) [r,g,b] = [c,x,0];
  else if (h < 120) [r,g,b] = [x,c,0];
  else if (h < 180) [r,g,b] = [0,c,x];
  else if (h < 240) [r,g,b] = [0,x,c];
  else if (h < 300) [r,g,b] = [x,0,c];
  else              [r,g,b] = [c,0,x];
  return [Math.round((r+m)*255), Math.round((g+m)*255), Math.round((b+m)*255)];
}

function rgbToHsv(r, g, b){
  r /= 255; g /= 255; b /= 255;
  const max = Math.max(r,g,b), min = Math.min(r,g,b), d = max - min;
  let h = 0;
  if (d !== 0){
    if      (max === r) h = 60 * (((g - b) / d) % 6);
    else if (max === g) h = 60 * ((b - r) / d + 2);
    else                 h = 60 * ((r - g) / d + 4);
  }
  if (h < 0) h += 360;
  const s = max === 0 ? 0 : d / max;
  return [h, s, max];
}

function drawWheel(){
  const canvas = document.getElementById('hueWheel');
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  const cx = w/2, cy = h/2, radius = w/2;
  const img = ctx.createImageData(w, h);
  for (let y = 0; y < h; y++){
    for (let x = 0; x < w; x++){
      const dx = x - cx, dy = y - cy;
      const dist = Math.sqrt(dx*dx + dy*dy);
      const idx = (y * w + x) * 4;
      if (dist <= radius){
        let angle = Math.atan2(dy, dx) * 180 / Math.PI;
        if (angle < 0) angle += 360;
        const sat = Math.min(1, dist / radius);
        const [r,g,b] = hsvToRgb(angle, sat, 1);
        img.data[idx]=r; img.data[idx+1]=g; img.data[idx+2]=b; img.data[idx+3]=255;
      }
    }
  }
  ctx.putImageData(img, 0, 0);
}

function positionCursor(hue, sat){
  const canvas = document.getElementById('hueWheel');
  const radius = canvas.width/2;
  const rad = hue * Math.PI/180;
  const dist = sat * radius;
  const x = radius + Math.cos(rad) * dist;
  const y = radius + Math.sin(rad) * dist;
  const cursor = document.getElementById('wheelCursor');
  cursor.style.left = x + 'px';
  cursor.style.top = y + 'px';
}

function setColorChannels(r, g, b){
  const chR = +document.getElementById('chR').value;
  const chG = +document.getElementById('chG').value;
  const chB = +document.getElementById('chB').value;
  const chDim = +document.getElementById('chDim').value;
  [[chR,r],[chG,g],[chB,b]].forEach(([ch,val]) => {
    updateFaderVisual(ch, val);
    sendChannel(ch, val);
  });
  if (chDim > 0) {
    updateFaderVisual(chDim, 255);
    sendChannel(chDim, 255);
  }
}

function updateColorFromWheel(){
  const brightness = document.getElementById('brightness').value / 255;
  const [r,g,b] = hsvToRgb(currentHue, currentSat, brightness);
  setColorChannels(r,g,b);
}

// Sets a color from anywhere other than dragging the wheel (swatches,
// presets) and syncs the wheel's own hue/sat/cursor + brightness slider
// to match. Without this, the wheel's stored hue/sat never changed when
// you clicked a swatch, so moving the brightness slider afterward would
// snap back to whatever the wheel last had instead of the swatch color.
function pickColor(r, g, b){
  const [h, s, v] = rgbToHsv(r, g, b);
  currentHue = h;
  currentSat = s;
  positionCursor(currentHue, currentSat);
  document.getElementById('brightness').value = Math.round(v * 255);
  setColorChannels(r, g, b);
}

function onBrightnessInput(){
  updateColorFromWheel();
}

function wheelPointer(e){
  const canvas = document.getElementById('hueWheel');
  const rect = canvas.getBoundingClientRect();
  const clientX = e.touches ? e.touches[0].clientX : e.clientX;
  const clientY = e.touches ? e.touches[0].clientY : e.clientY;
  const x = clientX - rect.left, y = clientY - rect.top;
  const cx = canvas.width/2, cy = canvas.height/2, radius = canvas.width/2;
  const dx = x - cx, dy = y - cy;
  let angle = Math.atan2(dy, dx) * 180/Math.PI;
  if (angle < 0) angle += 360;
  currentHue = angle;
  currentSat = Math.min(1, Math.sqrt(dx*dx+dy*dy) / radius);
  positionCursor(currentHue, currentSat);
  updateColorFromWheel();
}

function initWheel(){
  drawWheel();
  positionCursor(0, 0);
  const canvas = document.getElementById('hueWheel');
  canvas.draggable = false;
  let picking = false;
  // preventDefault on mousedown/mousemove stops the browser's native
  // image-drag gesture from hijacking the canvas — without it, Safari
  // (and sometimes Chrome) intercepts the drag for "drag this image out"
  // instead of firing mousemove, so the wheel only ever appeared to
  // update on the final mouseup.
  canvas.addEventListener('mousedown', e => { picking = true; wheelPointer(e); e.preventDefault(); });
  window.addEventListener('mousemove', e => { if (picking) { wheelPointer(e); e.preventDefault(); } });
  window.addEventListener('mouseup', () => picking = false);
  canvas.addEventListener('touchstart', e => { picking = true; wheelPointer(e); e.preventDefault(); }, {passive:false});
  canvas.addEventListener('touchmove', e => { if (picking) { wheelPointer(e); e.preventDefault(); } }, {passive:false});
  window.addEventListener('touchend', () => picking = false);
}

function buildColorPresets(){
  const presets = [
    [255,0,0],[0,255,0],[0,0,255],[255,255,0],
    [0,255,255],[255,0,255],[255,255,255],[0,0,0],
  ];
  const wrap = document.getElementById('colorPresets');
  presets.forEach(([r,g,b]) => {
    const btn = document.createElement('button');
    btn.className = 'swatch';
    btn.style.background = `rgb(${r},${g},${b})`;
    btn.onclick = () => pickColor(r,g,b);
    wrap.appendChild(btn);
  });
}

// ─── Effects ────────────────────────────────────────────────────────────────
const CHASE_COLORS = [[255,0,0],[0,255,0],[0,0,255],[255,255,0],[0,255,255],[255,0,255],[255,255,255]];

let effectInterval = null;
let effectName = null;
let effectPhase = 0;
let strobeOn = false;
let chaseIndex = -1;

function effectBtnId(name){
  return 'btn' + name.charAt(0).toUpperCase() + name.slice(1);
}

function getEffectSpeed(name){
  // Slider stores an interval in ms (smaller = faster), but that reads
  // backwards as a "speed" control — drag right (toward max) should mean
  // faster, not slower. Invert around the slider's own min/max so the
  // stored ms-interval semantics stay the same everywhere else.
  const el = document.getElementById('speed' + name.charAt(0).toUpperCase() + name.slice(1));
  const min = +el.min, max = +el.max, val = +el.value;
  return min + max - val;
}

function stopEffects(){
  if (effectInterval) { clearInterval(effectInterval); effectInterval = null; }
  document.querySelectorAll('.effectBtn').forEach(b => b.classList.remove('active'));
  effectName = null;
}

function startEffect(name){
  if (effectInterval) clearInterval(effectInterval);
  const speed = getEffectSpeed(name);
  if (name === 'rainbow'){
    effectInterval = setInterval(() => {
      effectPhase = (effectPhase + 4) % 360;
      const brightness = document.getElementById('brightness').value / 255;
      const [r,g,b] = hsvToRgb(effectPhase, 1, brightness);
      setColorChannels(r,g,b);
    }, speed);
  } else if (name === 'strobe'){
    effectInterval = setInterval(() => {
      strobeOn = !strobeOn;
      const v = strobeOn ? 255 : 0;
      setColorChannels(v,v,v);
    }, speed);
  } else if (name === 'pulse'){
    effectInterval = setInterval(() => {
      effectPhase = (effectPhase + 3) % 360;
      const b = (Math.sin(effectPhase * Math.PI/180) + 1) / 2;
      const [r,g,bb] = hsvToRgb(currentHue, currentSat, b);
      setColorChannels(r,g,bb);
    }, speed);
  } else if (name === 'chase'){
    effectInterval = setInterval(() => {
      chaseIndex = (chaseIndex + 1) % CHASE_COLORS.length;
      const [r,g,b] = CHASE_COLORS[chaseIndex];
      setColorChannels(r,g,b);
    }, speed);
  }
}

function toggleEffect(name){
  const btn = document.getElementById(effectBtnId(name));
  const wasActive = btn.classList.contains('active');
  stopEffects();
  if (wasActive) return;
  btn.classList.add('active');
  effectPhase = 0;
  strobeOn = false;
  chaseIndex = -1;
  effectName = name;
  startEffect(name);
}

function onSpeedChange(name){
  // Live-adjust the interval if this effect is the one currently running,
  // instead of waiting for the next toggle to pick up the new speed.
  if (effectName === name) startEffect(name);
}

function buildFaders(){
  const start = Math.min(512, Math.max(1, parseInt(document.getElementById('testStart').value) || 1));
  const count = Math.min(16, Math.max(1, parseInt(document.getElementById('testCount').value) || 10));
  const grid = document.getElementById('sliderGrid');
  grid.innerHTML='';
  for(let i=0;i<count;i++){
    const ch = start + i;
    if (ch > 512) break;
    const color = palette[i % palette.length];
    const s=document.createElement('div');
    s.className='strip';
    s.innerHTML = `
      <div class="tab" style="background:${color}">CH ${ch}</div>
      <div class="screen">
        <div class="row"><span>ADDR</span><b>${ch}</b></div>
        <div class="row"><span>VAL</span><b class="val">0</b></div>
        <div class="meter"><i></i></div>
      </div>
      <div class="knobrow"><div class="knob"></div><div class="knob"></div></div>
      <div class="track">
        <div class="fill"></div>
        <input type="range" min="0" max="255" value="0" data-ch="${ch}" oninput="onFaderInput(this)">
      </div>
      <div class="btnrow"><div class="dot"></div><div class="mutepill"></div></div>
      <div class="chnum">${ch}</div>
    `;
    grid.appendChild(s);
  }
}

function setAllFaders(v){
  document.querySelectorAll('#sliderGrid input[type=range]').forEach(el=>{el.value=v;onFaderInput(el);});
}

buildFaders();
initWheel();
buildColorPresets();
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    # HTTP/1.1 keep-alive — HTTP/1.0 (the default) opens a brand new TCP
    # connection for every single /set call, which was the main source of
    # latency when dragging a fader (many rapid requests).
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass  # keep stdout clean; errors still show via default error handling

    def _reply(self, code, content_type, body: bytes):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/":
            body = (
                PAGE.replace("__TARGET_IP__", self.server.target_ip)
                .replace("__ARTNET_PORT__", str(self.server.artnet_port))
                .replace("__UNIVERSE__", str(self.server.universe))
            ).encode()
            self._reply(200, "text/html", body)
        elif parsed.path == "/set":
            qs = parse_qs(parsed.query)
            try:
                ch = int(qs["ch"][0])
                val = int(qs["val"][0])
            except (KeyError, ValueError, IndexError):
                self._reply(400, "text/plain", b"bad request")
                return
            if 1 <= ch <= 512:
                val = max(0, min(255, val))
                with buffer_lock:
                    dmx_buffer[ch - 1] = val
                # Just wake the single sender thread — don't send from here.
                send_event.set()
            self._reply(200, "text/plain", b"ok")
        elif parsed.path == "/target":
            qs = parse_qs(parsed.query)
            ip = qs.get("ip", [""])[0].strip()
            try:
                universe = int(qs.get("universe", ["0"])[0])
            except ValueError:
                self._reply(400, "text/plain", b"bad universe")
                return
            if not ip or len(ip) > 255 or not (0 <= universe <= 32767):
                self._reply(400, "text/plain", b"bad ip or universe")
                return
            self.server.target_ip = ip
            self.server.universe = universe
            send_event.set()  # push current state to the new target right away
            self._reply(200, "text/plain", b"ok")
        else:
            self._reply(404, "text/plain", b"not found")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--target-ip", default="192.168.1.200",
                     help="Art-Net node's IP (default: 192.168.1.200)")
    ap.add_argument("--universe", type=int, default=0,
                     help="Art-Net universe (default: 0, must match the node's config)")
    ap.add_argument("--artnet-port", type=int, default=6454,
                     help="Art-Net UDP port (default: 6454, standard)")
    ap.add_argument("--http-port", type=int, default=8000,
                     help="local web server port (default: 8000)")
    args = ap.parse_args()

    server = ThreadingHTTPServer(("0.0.0.0", args.http_port), Handler)
    server.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.target_ip = args.target_ip
    server.artnet_port = args.artnet_port
    server.universe = args.universe

    sender = threading.Thread(
        target=sender_loop,
        args=(server,),
        daemon=True,
    )
    sender.start()

    print(f"Art-Net controller running at http://localhost:{args.http_port}")
    print(f"Sending to {args.target_ip}:{args.artnet_port}, universe {args.universe}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
