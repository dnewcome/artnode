#include "web_config.h"
#include <ArduinoJson.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------
// Embedded UI — single-page config interface
// ---------------------------------------------------------------------------
static const char INDEX_HTML[] PROGMEM = R"rawlit(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>artnode config</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:monospace;background:#111;color:#ccc;padding:1.5rem}
  h1{color:#fff;font-size:1.1rem;letter-spacing:.1em;margin-bottom:1.5rem}
  h2{font-size:.8rem;letter-spacing:.08em;color:#888;text-transform:uppercase;margin:1.5rem 0 .6rem}
  .card{background:#1a1a1a;border:1px solid #2a2a2a;border-radius:4px;padding:1rem;margin-bottom:1rem}
  label{display:block;font-size:.75rem;color:#888;margin-bottom:.25rem}
  input[type=text],input[type=password],input[type=number]{
    width:100%;background:#111;border:1px solid #333;color:#eee;
    padding:.4rem .6rem;border-radius:3px;font-family:monospace;font-size:.85rem}
  input:focus{outline:none;border-color:#555}
  input[type=range]{width:100%;accent-color:#4af}
  .strip-row{display:grid;grid-template-columns:1fr 1fr 1fr;gap:.5rem;margin-bottom:.5rem}
  .strip-label{font-size:.7rem;color:#666;margin-bottom:.15rem}
  button{background:#2a4a6a;color:#8cf;border:none;padding:.5rem 1.2rem;
    border-radius:3px;font-family:monospace;font-size:.85rem;cursor:pointer;margin-top:.5rem}
  button:hover{background:#3a5a7a}
  #status-box{font-size:.75rem;line-height:1.6;color:#888}
  #status-box span{color:#ccc}
  #save-msg{display:none;color:#4af;font-size:.8rem;margin-top:.5rem}
  .pin-note{font-size:.7rem;color:#555;margin-top:.25rem}
</style>
</head>
<body>
<h1>&#9675; artnode</h1>

<div class="card" id="status-box">loading&hellip;</div>

<h2>WiFi</h2>
<div class="card">
  <label>SSID</label>
  <input type="text" id="ssid" autocomplete="off">
  <label style="margin-top:.6rem">Password</label>
  <input type="password" id="pass" autocomplete="off">
  <label style="margin-top:.6rem">Hostname</label>
  <input type="text" id="hostname" autocomplete="off">
  <div class="pin-note">Accessible at hostname.local after save + reboot</div>
</div>

<h2>Network mode</h2>
<div class="card">
  <label>Node mode</label>
  <select id="node_mode" style="width:100%;background:#111;border:1px solid #333;color:#eee;padding:.4rem .6rem;border-radius:3px;font-family:monospace;font-size:.85rem">
    <option value="0">AUTO — WiFi if available, mesh fallback</option>
    <option value="1">BRIDGE — WiFi + forward to ESP-NOW mesh</option>
    <option value="2">DIRECT — WiFi only (no mesh)</option>
    <option value="3">MESH — ESP-NOW slave only (no WiFi)</option>
    <option value="4">STANDALONE — local patterns, no network</option>
  </select>
  <div class="pin-note">Change takes effect after reboot. MESH channel is compile-time (config.h).</div>
</div>

<h2>Output</h2>
<div class="card">
  <label>Brightness <span id="bri-val"></span></label>
  <input type="range" id="brightness" min="10" max="255">

  <h2 style="margin-top:1rem">Strips</h2>
  <div class="pin-note" style="margin-bottom:.6rem">Pin numbers are compile-time only — change in config.h and reflash</div>
  <div id="strips"></div>
</div>

<button id="save-btn">Save &amp; Reboot</button>
<div id="save-msg">Saved. Rebooting&hellip;</div>

<script>
let cfg = {};

async function loadStatus() {
  const r = await fetch('/api/status');
  const d = await r.json();
  document.getElementById('status-box').innerHTML =
    `IP: <span>${d.ip}</span> &nbsp; RSSI: <span>${d.rssi} dBm</span> &nbsp; ` +
    `Uptime: <span>${fmt(d.uptime_s)}</span> &nbsp; Frames: <span>${d.frames}</span>`;
}

function fmt(s) {
  const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), ss = s%60;
  return `${h}h${String(m).padStart(2,'0')}m${String(ss).padStart(2,'0')}s`;
}

async function loadConfig() {
  const r = await fetch('/api/config');
  cfg = await r.json();

  document.getElementById('ssid').value = cfg.ssid;
  document.getElementById('pass').value = cfg.password;
  document.getElementById('hostname').value = cfg.hostname;
  document.getElementById('node_mode').value = cfg.node_mode ?? 0;

  const bri = document.getElementById('brightness');
  bri.value = cfg.brightness;
  document.getElementById('bri-val').textContent = cfg.brightness;
  bri.oninput = () => document.getElementById('bri-val').textContent = bri.value;

  const container = document.getElementById('strips');
  container.innerHTML = '';
  cfg.strips.forEach((s, i) => {
    container.innerHTML += `
      <div style="margin-bottom:.75rem">
        <div class="strip-label">Strip ${i} (pin ${s.pin})</div>
        <div class="strip-row">
          <div><div class="strip-label">LEDs</div>
            <input type="number" id="s${i}_leds" value="${s.num_leds}" min="1" max="512"></div>
          <div><div class="strip-label">Universe</div>
            <input type="number" id="s${i}_uni" value="${s.start_universe}" min="0" max="255"></div>
          <div><div class="strip-label">Ch offset</div>
            <input type="number" id="s${i}_off" value="${s.channel_offset}" min="0" max="511"></div>
        </div>
      </div>`;
  });
}

document.getElementById('save-btn').addEventListener('click', async () => {
  const body = {
    ssid: document.getElementById('ssid').value,
    password: document.getElementById('pass').value,
    hostname: document.getElementById('hostname').value,
    brightness: parseInt(document.getElementById('brightness').value),
    node_mode:  parseInt(document.getElementById('node_mode').value),
    strips: cfg.strips.map((s, i) => ({
      num_leds:       parseInt(document.getElementById(`s${i}_leds`).value),
      start_universe: parseInt(document.getElementById(`s${i}_uni`).value),
      channel_offset: parseInt(document.getElementById(`s${i}_off`).value),
    }))
  };
  await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)});
  document.getElementById('save-msg').style.display = 'block';
  document.getElementById('save-btn').disabled = true;
});

loadConfig();
loadStatus();
setInterval(loadStatus, 5000);
</script>
</body>
</html>
)rawlit";

// ---------------------------------------------------------------------------

static uint32_t s_frames = 0;
static uint32_t s_start  = 0;

void WebConfig::begin() {
    s_start = millis();

    _server.on("/", HTTP_GET, [this]() { serveIndex(); });
    _server.on("/api/config", HTTP_GET,  [this]() { serveConfig(); });
    _server.on("/api/config", HTTP_POST, [this]() { handleSaveConfig(); });
    _server.on("/api/status", HTTP_GET,  [this]() { serveStatus(); });
    _server.begin();
}

void WebConfig::handle() {
    _server.handleClient();
}

// Called from Artnet callback so status page can show frame count
void webConfigCountFrame() { s_frames++; }

void WebConfig::serveIndex() {
    _server.send_P(200, "text/html", INDEX_HTML);
}

void WebConfig::serveConfig() {
    JsonDocument doc;
    doc["ssid"]       = _cfg.ssid;
    doc["password"]   = _cfg.password;
    doc["hostname"]   = _cfg.hostname;
    doc["brightness"] = _cfg.brightness;
    doc["node_mode"]  = (uint8_t)_cfg.node_mode;

    JsonArray strips = doc["strips"].to<JsonArray>();
    for (int i = 0; i < NUM_STRIPS; i++) {
        JsonObject s = strips.add<JsonObject>();
        s["num_leds"]       = _cfg.strips[i].num_leds;
        s["start_universe"] = _cfg.strips[i].start_universe;
        s["channel_offset"] = _cfg.strips[i].channel_offset;
        s["pin"]            = STRIPS[i].pin;   // read-only, compile-time
    }

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WebConfig::handleSaveConfig() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "text/plain", "no body");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
        _server.send(400, "text/plain", "bad json");
        return;
    }

    strlcpy(_cfg.ssid,     doc["ssid"]     | _cfg.ssid,     sizeof(_cfg.ssid));
    strlcpy(_cfg.password, doc["password"] | _cfg.password, sizeof(_cfg.password));
    strlcpy(_cfg.hostname, doc["hostname"] | _cfg.hostname, sizeof(_cfg.hostname));
    _cfg.brightness = doc["brightness"] | _cfg.brightness;
    if (doc["node_mode"].is<uint8_t>()) {
        _cfg.node_mode = (NodeMode)(uint8_t)doc["node_mode"];
    }

    JsonArray strips = doc["strips"];
    for (int i = 0; i < NUM_STRIPS && i < (int)strips.size(); i++) {
        _cfg.strips[i].num_leds       = strips[i]["num_leds"]       | _cfg.strips[i].num_leds;
        _cfg.strips[i].start_universe = strips[i]["start_universe"] | _cfg.strips[i].start_universe;
        _cfg.strips[i].channel_offset = strips[i]["channel_offset"] | _cfg.strips[i].channel_offset;
    }

    saveConfig(_cfg);
    _server.send(200, "text/plain", "ok");

    if (_onSave) _onSave(_cfg);

    delay(200);
    ESP.restart();
}

void WebConfig::serveStatus() {
    JsonDocument doc;
    doc["ip"]       = WiFi.localIP().toString();
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime_s"] = (millis() - s_start) / 1000;
    doc["frames"]   = s_frames;

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}
