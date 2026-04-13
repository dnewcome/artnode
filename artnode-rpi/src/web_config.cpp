#include "web_config.h"
#include "platform.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <cstdio>
#include <cstring>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Embedded UI — adapted from the ESP32 web_config.
// Changes vs ESP32 version:
//   • Node mode options limited to DIRECT and STANDALONE (no ESP-NOW mesh)
//   • "Save & Reboot" → "Save" (no reboot needed on Linux)
//   • Strip pins shown as GPIO BCM numbers (read-only, compile-time)
// ---------------------------------------------------------------------------
static const char INDEX_HTML[] = R"rawlit(<!DOCTYPE html>
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
  input[type=text],input[type=number]{
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

<h2>Hostname</h2>
<div class="card">
  <label>Hostname</label>
  <input type="text" id="hostname" autocomplete="off">
  <div class="pin-note">Accessible at hostname.local via mDNS (avahi) after save</div>
</div>

<h2>Network mode</h2>
<div class="card">
  <label>Node mode</label>
  <select id="node_mode" style="width:100%;background:#111;border:1px solid #333;color:#eee;padding:.4rem .6rem;border-radius:3px;font-family:monospace;font-size:.85rem">
    <option value="2">DIRECT — listen for Art-Net; patterns on idle</option>
    <option value="4">STANDALONE — local patterns only, no network</option>
  </select>
</div>

<h2>Output</h2>
<div class="card">
  <label>Brightness <span id="bri-val"></span></label>
  <input type="range" id="brightness" min="10" max="255">

  <h2 style="margin-top:1rem">Strips</h2>
  <div class="pin-note" style="margin-bottom:.6rem">GPIO numbers are BCM. Change in config.h and recompile to alter.</div>
  <div id="strips"></div>
</div>

<h2>Spatial mapping</h2>
<div class="card">
  <div class="pin-note" style="margin-bottom:.6rem">
    Place this node in a shared virtual canvas. All nodes running the same pattern
    will produce a coherent image across physical space. Set canvas width to 0 to
    disable (each strip runs patterns linearly).
  </div>
  <div class="strip-row">
    <div><label>Canvas W</label><input type="number" id="sp_w" min="0" max="65535"></div>
    <div><label>Canvas H</label><input type="number" id="sp_h" min="1" max="65535"></div>
  </div>
  <div class="strip-row">
    <div><label>Origin X</label><input type="number" id="sp_ox" step="0.01"></div>
    <div><label>Origin Y</label><input type="number" id="sp_oy" step="0.01"></div>
  </div>
  <div class="strip-row">
    <div><label>Step X / LED</label><input type="number" id="sp_sx" step="0.01"></div>
    <div><label>Step Y / LED</label><input type="number" id="sp_sy" step="0.01"></div>
  </div>
  <div class="pin-note">
    Example: 4 nodes &times; 60 LEDs across a 256-wide canvas, all at Y=128<br>
    node 0: origin 0,128 &middot; step 1.07,0 &nbsp; node 1: 64,128 &middot; step 1.07,0 &hellip;
  </div>
</div>

<button id="save-btn">Save</button>
<div id="save-msg">Saved.</div>

<script>
let cfg = {};

async function loadStatus() {
  const r = await fetch('/api/status');
  const d = await r.json();
  document.getElementById('status-box').innerHTML =
    `IP: <span>${d.ip}</span> &nbsp; Uptime: <span>${fmt(d.uptime_s)}</span> &nbsp; `+
    `Frames: <span>${d.frames}</span> &nbsp; FPS: <span>${d.fps}</span> &nbsp; `+
    `Source: <span style="color:${d.source==='artnet'?'#4f4':'#fa4'}">${d.source}</span> &nbsp; `+
    `Last universe: <span>${d.last_universe}</span>`;
}

function fmt(s) {
  const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), ss = s%60;
  return `${h}h${String(m).padStart(2,'0')}m${String(ss).padStart(2,'0')}s`;
}

async function loadConfig() {
  const r = await fetch('/api/config');
  cfg = await r.json();

  document.getElementById('hostname').value = cfg.hostname;
  document.getElementById('node_mode').value = cfg.node_mode ?? 2;

  document.getElementById('sp_w').value  = cfg.spatial?.virt_w   ?? 0;
  document.getElementById('sp_h').value  = cfg.spatial?.virt_h   ?? 256;
  document.getElementById('sp_ox').value = cfg.spatial?.origin_x ?? 0;
  document.getElementById('sp_oy').value = cfg.spatial?.origin_y ?? 128;
  document.getElementById('sp_sx').value = cfg.spatial?.step_x   ?? 1;
  document.getElementById('sp_sy').value = cfg.spatial?.step_y   ?? 0;

  const bri = document.getElementById('brightness');
  bri.value = cfg.brightness;
  document.getElementById('bri-val').textContent = cfg.brightness;
  bri.oninput = () => document.getElementById('bri-val').textContent = bri.value;

  const container = document.getElementById('strips');
  container.innerHTML = '';
  cfg.strips.forEach((s, i) => {
    container.innerHTML += `
      <div style="margin-bottom:.75rem">
        <div class="strip-label">Strip ${i} (GPIO ${s.gpio})</div>
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
    hostname:   document.getElementById('hostname').value,
    brightness: parseInt(document.getElementById('brightness').value),
    node_mode:  parseInt(document.getElementById('node_mode').value),
    strips: cfg.strips.map((s, i) => ({
      num_leds:       parseInt(document.getElementById(`s${i}_leds`).value),
      start_universe: parseInt(document.getElementById(`s${i}_uni`).value),
      channel_offset: parseInt(document.getElementById(`s${i}_off`).value),
    })),
    spatial: {
      virt_w:   parseInt(document.getElementById('sp_w').value),
      virt_h:   parseInt(document.getElementById('sp_h').value),
      origin_x: parseFloat(document.getElementById('sp_ox').value),
      origin_y: parseFloat(document.getElementById('sp_oy').value),
      step_x:   parseFloat(document.getElementById('sp_sx').value),
      step_y:   parseFloat(document.getElementById('sp_sy').value),
    }
  };
  const resp = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)});
  if (resp.ok) {
    document.getElementById('save-msg').style.display = 'block';
    setTimeout(() => document.getElementById('save-msg').style.display = 'none', 3000);
  }
});

loadConfig();
loadStatus();
setInterval(loadStatus, 5000);
</script>
</body>
</html>
)rawlit";

// ---------------------------------------------------------------------------

static std::string getLocalIP() {
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) == 0) {
        for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,
                      &reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr)->sin_addr,
                      buf, sizeof(buf));
            freeifaddrs(ifap);
            return buf;
        }
        freeifaddrs(ifap);
    }
    return "0.0.0.0";
}

WebConfig::~WebConfig() {
    if (_server) {
        _server->stop();
        delete _server;
    }
}

void WebConfig::begin(int port) {
    _startMs = millis();
    _server  = new httplib::Server();

    // GET /
    _server->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(INDEX_HTML, "text/html");
    });

    // GET /api/config
    _server->Get("/api/config", [this](const httplib::Request&, httplib::Response& res) {
        json j;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            j["hostname"]   = _cfg.hostname;
            j["brightness"] = _cfg.brightness;
            j["node_mode"]  = static_cast<int>(_cfg.node_mode);

            json strips = json::array();
            for (int i = 0; i < NUM_STRIPS; i++) {
                strips.push_back({
                    {"num_leds",       _cfg.strips[i].num_leds},
                    {"start_universe", _cfg.strips[i].start_universe},
                    {"channel_offset", _cfg.strips[i].channel_offset},
                    {"gpio",           STRIPS[i].gpio},  // compile-time, read-only
                });
            }
            j["strips"] = strips;

            j["spatial"] = {
                {"virt_w",   _cfg.spatial.virt_w},
                {"virt_h",   _cfg.spatial.virt_h},
                {"origin_x", _cfg.spatial.origin_x},
                {"origin_y", _cfg.spatial.origin_y},
                {"step_x",   _cfg.spatial.step_x},
                {"step_y",   _cfg.spatial.step_y},
            };
        }
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/config
    _server->Post("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto doc = json::parse(req.body);
            {
                std::lock_guard<std::mutex> lock(_mutex);

                if (doc.contains("hostname")) {
                    auto s = doc["hostname"].get<std::string>();
                    snprintf(_cfg.hostname, sizeof(_cfg.hostname), "%s", s.c_str());
                }
                if (doc.contains("brightness")) _cfg.brightness = doc["brightness"];
                if (doc.contains("node_mode"))  _cfg.node_mode  = (NodeMode)doc["node_mode"].get<int>();

                if (doc.contains("strips") && doc["strips"].is_array()) {
                    auto& arr = doc["strips"];
                    for (int i = 0; i < NUM_STRIPS && i < (int)arr.size(); i++) {
                        auto& s = arr[i];
                        if (s.contains("num_leds"))       _cfg.strips[i].num_leds       = s["num_leds"];
                        if (s.contains("start_universe")) _cfg.strips[i].start_universe = s["start_universe"];
                        if (s.contains("channel_offset")) _cfg.strips[i].channel_offset = s["channel_offset"];
                    }
                }

                if (doc.contains("spatial") && doc["spatial"].is_object()) {
                    auto& sp = doc["spatial"];
                    if (sp.contains("virt_w"))   _cfg.spatial.virt_w   = sp["virt_w"];
                    if (sp.contains("virt_h"))   _cfg.spatial.virt_h   = sp["virt_h"];
                    if (sp.contains("origin_x")) _cfg.spatial.origin_x = sp["origin_x"];
                    if (sp.contains("origin_y")) _cfg.spatial.origin_y = sp["origin_y"];
                    if (sp.contains("step_x"))   _cfg.spatial.step_x   = sp["step_x"];
                    if (sp.contains("step_y"))   _cfg.spatial.step_y   = sp["step_y"];
                }

                saveConfig(_cfg);
            }

            if (_onSave) _onSave(_cfg);
            res.set_content("ok", "text/plain");

        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(e.what(), "text/plain");
        }
    });

    // GET /api/status
    _server->Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        uint32_t elapsed_s = (millis() - _startMs.load()) / 1000;
        uint32_t frames    = _frames.load();
        double   fps       = elapsed_s > 0 ? static_cast<double>(frames) / elapsed_s : 0.0;
        json j;
        j["ip"]           = getLocalIP();
        j["uptime_s"]     = elapsed_s;
        j["frames"]       = frames;
        j["fps"]          = static_cast<int>(fps * 10) / 10.0;  // 1 decimal place
        j["source"]       = _source.load();
        j["last_universe"]= _lastUniverse.load();
        res.set_content(j.dump(), "application/json");
    });

    // Run server in background thread — cpp-httplib's listen() blocks.
    std::thread([this, port]() {
        if (!_server->listen("0.0.0.0", port)) {
            fprintf(stderr, "[web] failed to listen on port %d (try sudo)\n", port);
        }
    }).detach();

    printf("[web] http://%s.local  http://%s\n", _cfg.hostname, getLocalIP().c_str());
}
