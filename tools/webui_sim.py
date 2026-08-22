#!/usr/bin/env python3
"""
SEMS Web UI simulator.

Use this to compare the old lightweight UI against the current heavier team UI
without flashing ESP32 hardware.

Routes:
  http://127.0.0.1:8088/old/
  http://127.0.0.1:8088/new/
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse
import json
import time


PORT = 8088

state = {
    "device_name": "nocola_1",
    "mode": "Normal",
    "mqtt": {
        "enabled": True,
        "host": "128.199.206.166",
        "port": 1883,
        "username": "sems",
        "password": "secret",
        "client_id": "nocola_1",
        "base_topic": "trofis/enms",
        "publish_interval": 5,
        "connected": True,
        "rc": 0,
        "transport": "wifi",
    },
    "modbus": {
        "baudrate": 9600,
        "parity": 2,
        "stop_bits": 2,
        "poll_interval": 1000,
        "timeout": 500,
        "retry_count": 1,
        "profile": "PM1611",
        "slave_ids": [1, 2, 3],
        "presets": ["p1_1ph", "p1_3ph", "p1_1ph"],
        "active_index": 0,
        "phase": 1,
        "register_template": "MIXED_PRESET",
    },
    "relays": [0, 1, 0, 0],
    "meters": [
        {"slave_id": 1, "online": True, "valid": True, "voltage": 223.4, "current": 4.21, "power": 812.5, "energy": 142.83},
        {"slave_id": 2, "online": True, "valid": True, "voltage": 221.8, "current": 3.76, "power": 704.9, "energy": 98.44},
        {"slave_id": 3, "online": False, "valid": False, "voltage": 0, "current": 0, "power": 0, "energy": 0},
    ],
    "last_push": None,
}


OLD_STYLE = """
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#f1f5f9;min-height:100vh;padding:16px;color:#0f172a}
.wrap{max-width:480px;margin:0 auto}
h1{font-size:18px;font-weight:700;color:#0f172a;margin-bottom:16px}
.card{background:#fff;border-radius:12px;padding:16px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
.card-title,.k{font-size:11px;font-weight:600;color:#94a3b8;text-transform:uppercase;letter-spacing:.05em;margin-bottom:10px}
.row{display:flex;justify-content:space-between;align-items:center;padding:4px 0;gap:8px;flex-wrap:wrap}
.label{font-size:13px;color:#64748b}.val{font-size:13px;font-weight:600;color:#0f172a;text-align:right;word-break:break-all}
.badge{display:inline-flex;align-items:center;gap:5px;font-size:12px;font-weight:600;padding:3px 10px;border-radius:99px}
.up{background:#dcfce7;color:#15803d}.down{background:#fee2e2;color:#b91c1c}.connecting{background:#fef9c3;color:#92400e}
.dot{width:6px;height:6px;border-radius:50%;background:currentColor}.sep{height:1px;background:#f1f5f9;margin:8px 0}
label{display:block;font-size:12px;font-weight:600;color:#475569;margin:10px 0 4px}
input,select{width:100%;border:1px solid #cbd5e1;border-radius:8px;padding:8px 10px;font-size:14px;background:#fff}
.btn,a,button{display:inline-block;background:#0f766e;color:#fff;border:0;border-radius:8px;padding:9px 16px;font-size:14px;font-weight:600;cursor:pointer;margin:4px 4px 0 0;text-decoration:none}
.btn-sm{padding:5px 12px;font-size:12px}.btn-danger{background:#b91c1c}.btn-ghost{background:#e2e8f0;color:#0f172a}
.nav{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:12px}.nav a{font-size:12px;padding:7px 10px}
.ok{color:#15803d}.err,.bad{color:#b91c1c}.v{font-size:22px;font-weight:700;color:#0f766e;line-height:1.1}
.muted{font-size:13px;color:#64748b}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px;margin-top:8px}
.mv{background:#f8fafc;border:1px solid #e2e8f0;border-radius:10px;padding:10px 12px;text-align:center}
.ml{font-size:11px;color:#64748b;margin-bottom:2px}.mn{font-size:22px;font-weight:700;color:#0f766e;line-height:1.1}.mu{font-size:11px;color:#94a3b8}
table{width:100%;border-collapse:collapse}th,td{font-size:12px;text-align:left;border-bottom:1px solid #e2e8f0;padding:7px}
code{background:#eef2f7;border-radius:4px;padding:1px 4px}
</style>
"""


NEW_STYLE = OLD_STYLE + """
<style>
.feature{background:#ecfeff;border:1px solid #a5f3fc;color:#155e75}
.topic-list{list-style:none;margin:8px 0 0;padding:0}
.topic-list li{font-size:12px;color:#475569;border-top:1px solid #f1f5f9;padding:7px 0;line-height:1.35}
.actions{display:flex;gap:6px;flex-wrap:wrap;margin-top:10px}
.wide{width:100%;margin-top:14px}
.mode-switch{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-top:10px}
.mode-switch button{margin:0;width:100%;background:#e2e8f0;color:#0f172a}
.mode-switch button.active-normal{background:#0f766e;color:#fff}
.mode-switch button.active-config{background:#b45309;color:#fff}
</style>
"""


def base_topic() -> str:
    return f"{state['mqtt']['base_topic']}/{state['device_name']}"


PRESET_LABELS = {
    "p1_1ph": "PM2230 / PM2120 / EM6400 - 1-Phase",
    "p1_3ph": "PM2230 / EM6400 - 3-Phase",
    "renata_3ph": "Renata AX9L - 3-Phase (INT32)",
}


def preset_label(value: str) -> str:
    return PRESET_LABELS.get(value, PRESET_LABELS["p1_1ph"])


def preset_options(selected: str) -> str:
    return "".join(
        f"<option value='{value}' {'selected' if selected == value else ''}>{label}</option>"
        for value, label in PRESET_LABELS.items()
    )


def nav(prefix: str) -> str:
    if prefix == "old":
        home = "" if state["mode"] == "Config" else "<a href='/old/'>&#127968; Home</a>"
        reboot = ""
        if state["mode"] == "Config":
            reboot = "<a href='#' onclick='if(confirm(\"Reboot device ke Normal Mode?\"))fetch(\"/api/mode/toggle\",{method:\"POST\"}).then(()=>location.reload())' style='background:#fee2e2;color:#b91c1c;margin-left:auto'>&#8635; Reboot (Normal Mode)</a>"
        return (
            f"<div class=nav>{home}<a href='/old/network'>&#127760; Network</a>"
            "<a href='/old/mqtt'>&#128236; MQTT</a>"
            "<a href='/old/modbus'>&#128268; Modbus</a>"
            f"<a href='/old/update'>&#128229; OTA</a>{reboot}</div>"
        )
    cls = ""
    return (
        f"<nav class='{cls} nav'><a href='/{prefix}/'>Status</a><a href='/{prefix}/mqtt'>MQTT</a>"
        f"<a href='/{prefix}/modbus'>Modbus</a><a href='/{prefix}/protection'>Relay</a>"
        f"<a href='/'>Compare</a></nav>"
    )


def shell(prefix: str, title: str, body: str) -> bytes:
    style = OLD_STYLE if prefix == "old" else NEW_STYLE
    if prefix in ("old", "new"):
        head = f"<h1>{title}</h1>{nav(prefix)}"
    html = f"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>{style}<title>{title}</title></head><body><div class='wrap'>{head}{body}</div></body></html>"
    return html.encode("utf-8")


def new_mode_switch() -> str:
    normal_cls = "active-normal" if state["mode"] != "Config" else ""
    config_cls = "active-config" if state["mode"] == "Config" else ""
    return f"""
    <div class="mode-switch">
      <button class="{normal_cls}" type="button" onclick="setMode('Normal')">Normal</button>
      <button class="{config_cls}" type="button" onclick="setMode('Config')">Config</button>
    </div>
    <script>
    async function setMode(mode){{
      if ((mode === 'Config') === ({'true' if state['mode'] == 'Config' else 'false'})) return;
      await fetch('/api/mode/set', {{method:'POST', body:new URLSearchParams({{mode}})}});
      location.reload();
    }}
    </script>
    """


def compare_page() -> bytes:
    body = """
    <style>
    :root{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#f5f7fb;color:#172033}
    body{margin:0}.wrap{max-width:900px;margin:0 auto;padding:24px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}
    a{display:block;background:#fff;border:1px solid #dbe3ef;border-radius:10px;padding:18px;text-decoration:none;color:#172033}
    h1{font-size:24px}.tag{font-size:12px;font-weight:800;color:#0f766e}.muted{color:#64748b}
    </style><div class='wrap'><h1>SEMS Web UI Simulator</h1><div class='grid'>
    <a href='/old/'><div class='tag'>OLD</div><h2>Lightweight Web UI</h2><p class='muted'>Gaya sebelum pull branch team: ringkas, operasional, sedikit teks.</p></a>
    <a href='/new/'><div class='tag'>NEW</div><h2>Current Team Web UI</h2><p class='muted'>Gaya branch sekarang: lebih banyak panel, notes, preview topic, dan diagnostic copy.</p></a>
    </div></div>
    """
    return f"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>SEMS UI Compare</title></head><body>{body}</body></html>".encode()


def status_page(prefix: str) -> bytes:
    if prefix == "old":
        config_button = "<button class='btn btn-sm btn-danger' id=btnCfg type=button>&#128268; Masuk Config Mode (AP)</button> " if state["mode"] != "Config" else "<button class='btn btn-sm btn-ghost' style='cursor:not-allowed;opacity:0.6' disabled type=button>Sudah di Config Mode (AP)</button> "
        body = f"""
        <section class="card"><div class="card-title">Hardware</div>
          <div class="row"><span class="label">Chip</span><span class="val">ESP32 ESP32-D0WDQ6 rev1</span></div><div class="sep"></div>
          <div class="row"><span class="label">Flash</span><span class="val">4096 kB</span></div>
          <div class="row"><span class="label">Free Heap</span><span class="val">218 kB</span></div><div class="sep"></div>
          <div class="row"><span class="label">MAC</span><span class="val" style="font-family:monospace;font-size:12px">24:6F:28:AA:BB:CC</span></div>
          <div class="row"><span class="label">Firmware</span><span class="val">v2.2.0</span></div>
          <div class="row"><span class="label">Uptime</span><span class="val">00h12m34s</span></div>
        </section>
        <section class="card"><div class="card-title">Konfigurasi</div>
          <a class="btn btn-sm" href="/old/network" style="text-decoration:none">&#127760; Network &amp; WiFi</a>
          <a class="btn btn-sm" href="/old/mqtt" style="text-decoration:none">&#128236; MQTT</a>
        </section>
        <section class="card"><div class="card-title">Waktu &amp; Timezone</div>
          <div class="row"><span class="label">Timezone</span><span class="val">UTC +7</span></div>
        </section>
        <section class="card"><div class="card-title">Sistem</div>
          {config_button}<button class="btn btn-sm btn-danger" id=btnRbt type=button>&#8635; Reboot</button>
        </section>
        <script>
        const btnCfg=document.getElementById('btnCfg');
        if(btnCfg)btnCfg.onclick=async()=>{{
          if(!confirm('Masuk Config Mode? Device akan restart dan broadcast AP.'))return;
          await fetch('/api/mode/toggle', {{method:'POST'}});
          document.body.innerHTML='<div class=wrap><div class=card><h3>Restarting...</h3><p style="font-size:13px;color:#64748b;margin-top:8px">Device sedang memuat ulang ke Config Mode. Hubungkan ke AP SEMS-SETUP-xx jika menggunakan WiFi.</p></div></div>';
          setTimeout(()=>location.reload(),900);
        }};
        document.getElementById('btnRbt').onclick=async()=>{{
          if(!confirm('Reboot device?'))return;
          await fetch('/api/reboot', {{method:'POST'}});
        }};
        </script>
        """
        return shell(prefix, "SEMS AIoT", body)

    meters = "".join(
        f"<tr><td>Slave {m['slave_id']}</td><td class='{'ok' if m['online'] else 'bad'}'>{'ONLINE' if m['online'] else 'OFFLINE'}</td><td>{m['voltage']:.1f} V</td><td>{m['current']:.2f} A</td><td>{m['energy']:.2f} kWh</td></tr>"
        for m in state["meters"]
    )
    cls = "card"
    title_cls = "card-title"
    metric_cls = "v"
    mode_note = (
        "Runtime aktif. Konfigurasi MQTT/Modbus dikunci agar polling dan web-app link stabil."
        if state["mode"] != "Config"
        else "Config aktif. Aman untuk edit parameter; runtime hardware disimulasikan berhenti."
    )
    body = f"""
    <section class="{cls}" style="background:{'#fee2e2' if state['mode'] != 'Config' else '#fef9c3'};border:1px solid {'#fca5a5' if state['mode'] != 'Config' else '#fde047'}">
      <div class="{title_cls}">Mode Operasi</div>
      <div class="row"><span class="label">Current Mode</span><span class="badge {'down' if state['mode'] != 'Config' else 'connecting'}"><span class="dot"></span>{state['mode']}</span></div>
      <p class="muted">{mode_note}</p>
      {new_mode_switch()}
    </section>
    <section class="{cls}"><div class="{title_cls}">Runtime</div>
      <div class="row"><span class="label">Mode</span><span class="val">{state['mode']}</span></div>
      <div class="row"><span class="label">MQTT</span><span class="badge {'up' if state['mqtt']['connected'] else 'down'}"><span class="dot"></span>{'Connected' if state['mqtt']['connected'] else 'Offline'}</span></div>
      <div class="row"><span class="label">Base Topic</span><span class="val">{base_topic()}</span></div>
    </section>
    <section class="{cls}"><div class="{title_cls}">Fitur Branch Baru</div>
      <div class="row"><span class="label">Meter Type</span><span class="val">PM1611 sejenis</span></div>
      <div class="row"><span class="label">Web-app control</span><span class="badge feature"><span class="dot"></span>control/slave_N</span></div>
      <div class="row"><span class="label">Push data</span><span class="val">manual + interval</span></div>
      <div class="row"><span class="label">Relay state</span><span class="val">{' '.join(str(x) for x in state['relays'])}</span></div>
    </section>
    <section class="{cls}"><div class="{title_cls}">Meter Snapshot</div><table><tr><th>Meter</th><th>Status</th><th>Voltage</th><th>Current</th><th>Energy</th></tr>{meters}</table></section>
    """
    return shell(prefix, "SEMS AIoT", body)


def mqtt_old_page() -> bytes:
    m = state["mqtt"]
    badge = "up" if m["connected"] else ("connecting" if m["enabled"] else "down")
    mode_banner = "" if state["mode"] == "Config" else """
    <section class="card" style="background:#fee2e2;border:1px solid #fca5a5">
      <div style="font-weight:600;color:#991b1b;font-size:14px;margin-bottom:4px">Normal Mode Aktif</div>
      <div class="muted" style="color:#7f1d1d;margin-bottom:8px">Konfigurasi MQTT hanya dapat diubah di Config Mode.</div>
      <button class="btn btn-sm btn-danger" type="button" onclick="toggleMode()">Masuk Config Mode</button>
    </section>
    """
    body = f"""
    {mode_banner}
    <section class="card"><div class="card-title">Status MQTT</div>
      <div class="row"><span class="label">Koneksi</span><span class="badge {badge}"><span class="dot"></span>{'Connected' if m['connected'] else 'Disconnected'}</span></div>
      <div class="row"><span class="label">Broker</span><span class="val">{m['host']}:{m['port']}</span></div>
      <div class="row"><span class="label">Topic</span><span class="val">{base_topic()}</span></div>
    </section>
    <section class="card"><div class="card-title">Konfigurasi Broker</div>
      <label><input id="mqtt_enabled" type="checkbox" {'checked' if m['enabled'] else ''}> MQTT Aktif</label>
      <label>Host/IP Broker</label><input id="mqtt_host" value="{m['host']}">
      <label>Port</label><input id="mqtt_port" type="number" value="{m['port']}">
      <label>Username</label><input value="{m['username']}">
      <label>Password</label><input type="password" value="{m['password']}">
      <label>Topic Prefix</label><input id="mqtt_base_topic" value="{m['base_topic']}">
      <label>Device Name</label><input id="device_name" value="{state['device_name']}">
      <label>Publish Interval (menit)</label><input id="mqtt_publish_interval" type="number" value="{m['publish_interval']}">
      <div class="row"><button onclick="saveMqtt()">Simpan</button><button class="btn-ghost" onclick="pushNow()">Push Now</button></div>
      <p id="mqtt_msg" class="muted">Ready.</p>
    </section>
    {common_mqtt_script()}
    <script>
    async function toggleMode(){{
      await fetch('/api/mode/toggle', {{method:'POST'}});
      location.reload();
    }}
    </script>
    """
    return shell("old", "SEMS AIoT &mdash; MQTT", body)


def mqtt_new_page() -> bytes:
    m = state["mqtt"]
    readonly = "disabled" if state["mode"] != "Config" else ""
    mode_banner = (
        f"""
        <section class="card" style="background:#fee2e2;border:1px solid #fca5a5">
          <div style="font-weight:600;color:#991b1b;font-size:14px;margin-bottom:4px">Normal Mode Aktif</div>
          <div class="muted" style="color:#7f1d1d;margin-bottom:8px">Konfigurasi MQTT dikunci. Push Data Now tetap tersedia untuk simulasi web-app.</div>
          {new_mode_switch()}
        </section>
        """
        if state["mode"] != "Config"
        else f"""
        <section class="card" style="background:#fef9c3;border:1px solid #fde047">
          <div style="font-weight:600;color:#854d0e;font-size:14px;margin-bottom:4px">Config Mode</div>
          <div class="muted" style="color:#713f12;margin-bottom:8px">Edit MQTT aktif. Reboot ke Normal Mode untuk runtime produksi.</div>
          {new_mode_switch()}
        </section>
        """
    )
    meter_rows = "".join(
        f"<li>PM1611 Slave {sid}: <code>{base_topic()}/elc_data/slave_{sid}</code><br><code>{base_topic()}/elc_wh/slave_{sid}</code></li>"
        for sid in state["modbus"]["slave_ids"]
    )
    relay_rows = "".join(
        f"<li>PM1611 Slave {i}: <code>{base_topic()}/control/slave_{i}</code> -> <code>0</code>/<code>1</code><br>State: <code>{base_topic()}/control/slave_{i}/state</code></li>"
        for i in range(1, 5)
    )
    body = f"""
    {mode_banner}
    <section class="card"><div class="card-title">Status MQTT</div>
      <div class="row"><span class="label">Koneksi</span><span class="badge {'up' if m['connected'] else 'down'}"><span class="dot"></span>{'Connected' if m['connected'] else 'Disconnected'}</span></div>
      <div class="row"><span class="label">Broker</span><span class="val">{m['host']}:{m['port']}</span></div>
      <div class="row"><span class="label">RC / Transport</span><span class="val">{m['rc']} / {m['transport']}</span></div>
      <div class="row"><span class="label">Client ID</span><span class="val">{m['client_id']}</span></div>
    </section>
    <section class="card"><div class="card-title">Konfigurasi Broker</div>
      <label><input id="mqtt_enabled" type="checkbox" {'checked' if m['enabled'] else ''} {readonly}> MQTT Aktif</label>
      <label>Host / IP Broker</label><input id="mqtt_host" value="{m['host']}" {readonly}>
      <label>Port</label><input id="mqtt_port" type="number" value="{m['port']}" {readonly}>
      <label>Username</label><input value="{m['username']}" {readonly}>
      <label>Password</label><input type="password" value="{m['password']}" {readonly}>
      <label>Topic Prefix</label><input id="mqtt_base_topic" value="{m['base_topic']}" {readonly}>
      <label>Device Name</label><input id="device_name" value="{state['device_name']}" {readonly}>
      <label>Publish Interval (menit)</label><input id="mqtt_publish_interval" type="number" value="{m['publish_interval']}" {readonly}>
      <button class="btn wide" onclick="saveMqtt()" {readonly}>Simpan</button><p id="mqtt_msg" class="muted">Ready.</p>
    </section>
    <section class="card"><div class="card-title">Topic Produksi - Meter Sejenis</div>
      <div class="row"><span class="label">Active Prefix</span><span class="val"><code>{base_topic()}</code></span></div>
      <ul class="topic-list">{meter_rows}</ul>
    </section>
    <section class="card"><div class="card-title">Web-app Control per Slave</div>
      <ul class="topic-list">{relay_rows}</ul>
    </section>
    <section class="card"><div class="card-title">Push Data &amp; Loopback</div>
      <div class="row"><span class="label">Test Topic</span><span class="val"><code>{base_topic()}/test</code></span></div>
      <div class="actions"><button class="btn" onclick="pushNow()">Push Data Now</button></div>
      <p class="muted">Publish state, elc_data, elc_wh, control-state, lalu cek echo terpisah.</p>
    </section>
    {common_mqtt_script()}
    """
    return shell("new", "SEMS AIoT &mdash; MQTT", body)


def common_mqtt_script() -> str:
    return """
    <script>
    async function saveMqtt(){
      const body = new URLSearchParams({
        mqtt_enabled: document.getElementById('mqtt_enabled').checked ? '1':'0',
        mqtt_host: document.getElementById('mqtt_host').value,
        mqtt_port: document.getElementById('mqtt_port').value,
        mqtt_base_topic: document.getElementById('mqtt_base_topic').value,
        mqtt_publish_interval: document.getElementById('mqtt_publish_interval')?.value || '5',
        device_name: document.getElementById('device_name').value
      });
      const d = await (await fetch('/api/mqtt/save', {method:'POST', body})).json();
      document.getElementById('mqtt_msg').textContent = d.ok ? 'Saved in simulator.' : 'Save failed';
      if(d.ok) setTimeout(()=>location.reload(), 350);
    }
    async function pushNow(){
      const d = await (await fetch('/api/mqtt/test_publish', {method:'POST'})).json();
      const el = document.getElementById('mqtt_msg');
      if(el) el.textContent = d.ok ? 'Published mock payload.' : 'Publish failed';
    }
    </script>
    """


def modbus_page(prefix: str) -> bytes:
    cfg = state["modbus"]
    if prefix == "old":
        if state["mode"] == "Config":
            body = f"""
            <section class="card" style="background:#fef9c3;border:1px solid #fde047;padding:12px;margin-bottom:12px">
              <div style="font-weight:600;color:#854d0e;font-size:14px;margin-bottom:4px">Config Mode</div>
              <div style="font-size:12px;color:#713f12">Perubahan baud rate &amp; tipe meter aktif setelah reboot.</div>
            </section>
            <section class="card"><div class="card-title">Status Meter</div><div id=status>Loading...</div></section>
            <section class="card"><div class="card-title">Test Koneksi (Ping Register)</div>
              <div style="font-size:12px;color:#64748b;margin-bottom:8px">Baca 1 register langsung - cek respon RTU tanpa nunggu polling penuh. Berguna untuk debug wiring/baud/parity.</div>
              <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px 16px">
                <div><label>Slave ID</label><input type=number id=pingSlave min=1 max=247 placeholder="(pakai slave aktif)"></div>
                <div><label>Alamat Register (desimal)</label><input type=number id=pingAddr min=0 max=65535 value=16384></div>
              </div>
              <button class="btn btn-sm" style="margin-top:8px">Ping</button>
              <div style="margin-top:8px;font-size:13px;font-family:monospace;white-space:pre-wrap"></div>
            </section>
            <section class="card"><div class="card-title">Preset</div>
              <div style="font-size:12px;color:#64748b;margin-bottom:8px">Isi register map otomatis. Semua preset: FC03, FP32 big-endian, addr 0-based.</div>
              <div style="display:flex;gap:8px;flex-wrap:wrap">
                <button class="btn btn-sm">PM2230 / PM2120 / EM6400 - 1-Phase</button>
                <button class="btn btn-sm">PM2230 / EM6400 - 3-Phase</button>
                <button class="btn btn-sm">Renata AX9L - 3-Phase (INT32)</button>
              </div>
            </section>
            <section class="card"><div class="card-title">Koneksi RS485</div>
              <label>Slave ID (1-247)</label><input type=number id=slave min=1 max=247 value="{cfg['slave_ids'][0]}">
              <label>Baud Rate</label><select id=baud><option>1200</option><option>2400</option><option>4800</option><option selected>9600</option><option>19200</option><option>38400</option><option>115200</option></select>
              <label>Poll Interval (ms)</label><input type=number id=poll min=200 max=60000 value="{cfg['poll_interval']}">
              <label>Mode Fasa</label>
              <div style="display:flex;gap:12px;margin-top:4px">
                <label style="display:flex;align-items:center;gap:6px;font-size:14px;color:#1e293b"><input type=radio name=phase checked> 1-Phase</label>
                <label style="display:flex;align-items:center;gap:6px;font-size:14px;color:#1e293b"><input type=radio name=phase> 3-Phase</label>
              </div>
            </section>
            <script>document.getElementById('status').innerHTML='<span class="badge up"><span class=dot></span>Simulated</span>';</script>
            """
            return shell(prefix, "SEMS AIoT &mdash; Modbus", body)

        meter_cards = "".join(
            f"""
            <section class="card"><div class="card-title">PM1611 Slave {m['slave_id']}</div>
              <div class="row"><span class="label">Status</span><span class="badge {'up' if m['online'] else 'down'}"><span class="dot"></span>{'ONLINE' if m['online'] else 'OFFLINE'}</span></div>
              <div class="grid">
                <div class="mv"><div class="ml">Voltage</div><div class="mn">{m['voltage']:.1f}</div><div class="mu">V</div></div>
                <div class="mv"><div class="ml">Current</div><div class="mn">{m['current']:.2f}</div><div class="mu">A</div></div>
                <div class="mv"><div class="ml">Power</div><div class="mn">{m['power']:.0f}</div><div class="mu">W</div></div>
                <div class="mv"><div class="ml">Energy</div><div class="mn">{m['energy']:.2f}</div><div class="mu">kWh</div></div>
              </div>
            </section>
            """
            for m in state["meters"]
        )
        body = f"""
        <section class="card" style="background:#fee2e2;border:1px solid #fca5a5;padding:10px 12px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px">
          <div>
            <div style="font-weight:600;color:#991b1b;font-size:13px">Normal Mode - Konfigurasi terkunci</div>
            <div style="font-size:11px;color:#7f1d1d">Masuk Config Mode untuk ubah register map / baud rate</div>
          </div>
          <button class="btn btn-sm btn-danger" type=button onclick="fetch('/api/mode/toggle',{{method:'POST'}}).then(()=>location.reload())">Config Mode</button>
        </section>
        <section class="card"><div style="display:flex;align-items:center;justify-content:space-between">
          <div class="card-title" style="margin:0">Pembacaan Meter</div>
          <span><span class="badge up"><span class=dot></span>1-Phase</span></span>
        </div></section>
        {meter_cards}
        """
        return shell(prefix, "SEMS AIoT &mdash; Modbus", body)

    cls = "card"
    title_cls = "card-title"
    active_slave = cfg["slave_ids"][cfg["active_index"] % len(cfg["slave_ids"])]
    active_preset = cfg["presets"][cfg["active_index"] % len(cfg["presets"])] if cfg["presets"] else "p1_1ph"
    if state["mode"] != "Config":
        meter_cards = "".join(
            f"""
            <section class="card"><div class="card-title">PM1611 Slave {m['slave_id']}</div>
              <div class="row"><span class="label">Status</span><span class="badge {'up' if m['online'] else 'down'}"><span class="dot"></span>{'ONLINE' if m['online'] else 'OFFLINE'}</span></div>
              <div class="row"><span class="label">Preset</span><span class="val">{preset_label(cfg['presets'][idx] if idx < len(cfg['presets']) else 'p1_1ph')}</span></div>
              <div class="row"><span class="label">Poll Slot</span><span class="val">{(m['slave_id'] - 1) * cfg['poll_interval']} ms</span></div>
              <div class="grid">
                <div class="mv"><div class="ml">Voltage</div><div class="mn">{m['voltage']:.1f}</div><div class="mu">V</div></div>
                <div class="mv"><div class="ml">Current</div><div class="mn">{m['current']:.2f}</div><div class="mu">A</div></div>
                <div class="mv"><div class="ml">Power</div><div class="mn">{m['power']:.0f}</div><div class="mu">W</div></div>
                <div class="mv"><div class="ml">Energy</div><div class="mn">{m['energy']:.2f}</div><div class="mu">kWh</div></div>
              </div>
            </section>
            """
            for idx, m in enumerate(state["meters"])
        )
        body = f"""
        <section class="card" style="background:#fee2e2;border:1px solid #fca5a5;padding:10px 12px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px">
          <div>
            <div style="font-weight:600;color:#991b1b;font-size:13px">Normal Mode - Konfigurasi terkunci</div>
            <div style="font-size:11px;color:#7f1d1d">Polling PM1611 multi-slave aktif. Masuk Config Mode untuk ubah RS485 / slave list.</div>
          </div>
          {new_mode_switch()}
        </section>
        <section class="card"><div style="display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap">
          <div class="card-title" style="margin:0">Pembacaan Meter</div>
          <span><span class="badge up"><span class=dot></span>Mixed 1P/3P</span></span>
        </div><div class="sep"></div>
          <div class="row"><span class="label">Active Cursor</span><span class="badge feature"><span class="dot"></span>Slave {active_slave}</span></div>
          <div class="row"><span class="label">Strategy</span><span class="val">old active-slave serial + per-slot converter</span></div>
          <div class="row"><span class="label">Interval</span><span class="val">{cfg['poll_interval']} ms per slot</span></div>
        </section>
        {meter_cards}
        <section class="card"><div class="card-title">Web-app Control</div>
          <div class="row"><span class="label">Control Topic</span><span class="val"><code>{base_topic()}/control/slave_N</code></span></div>
          <div class="row"><span class="label">State Topic</span><span class="val"><code>{base_topic()}/control/slave_N/state</code></span></div>
        </section>
        """
        return shell(prefix, "SEMS AIoT &mdash; Modbus", body)

    readonly = "disabled" if state["mode"] != "Config" else ""
    mode_banner = (
        f"""
        <section class="card" style="background:#fee2e2;border:1px solid #fca5a5">
          <div style="font-weight:600;color:#991b1b;font-size:14px;margin-bottom:4px">Normal Mode Aktif</div>
          <div class="muted" style="color:#7f1d1d;margin-bottom:8px">Konfigurasi Modbus dikunci. Polling multi-slave mengikuti active-slave cursor.</div>
          {new_mode_switch()}
        </section>
        """
        if state["mode"] != "Config"
        else f"""
        <section class="card" style="background:#fef9c3;border:1px solid #fde047">
          <div style="font-weight:600;color:#854d0e;font-size:14px;margin-bottom:4px">Config Mode</div>
          <div class="muted" style="color:#713f12;margin-bottom:8px">Edit RS485 dan slave list aktif. Reboot ke Normal Mode untuk polling produksi.</div>
          {new_mode_switch()}
        </section>
        """
    )
    schedule_rows = "".join(
        f"<tr><td>PM {idx + 1}</td><td>Slave {sid}</td><td>{preset_label(cfg['presets'][idx] if idx < len(cfg['presets']) else 'p1_1ph')}</td><td>{'active' if sid == active_slave else 'wait'}</td></tr>"
        for idx, sid in enumerate(cfg["slave_ids"])
    )
    pm_count_options = "".join(
        f"<option value='{n}' {'selected' if len(cfg['slave_ids']) == n else ''}>{n} PM</option>"
        for n in range(1, 5)
    )
    device_rows = "".join(
        f"""
        <div class="card" style="box-shadow:none;border:1px solid #e2e8f0;padding:10px;margin-top:8px">
          <div class="card-title">Urutan PM {idx + 1}</div>
          <label>Slave ID</label><input type="number" class="pm-slave" min="1" max="247" value="{sid}">
          <label>Preset Register Map</label><select class="pm-type">
            {preset_options(cfg['presets'][idx] if idx < len(cfg['presets']) else 'p1_1ph')}
          </select>
        </div>
        """
        for idx, sid in enumerate(cfg["slave_ids"])
    )
    intro = f"<div class='row'><span class='label'>Meter Type</span><span class='val'>{cfg['profile']} sejenis</span></div><div class='row'><span class='label'>Strategy</span><span class='badge feature'><span class='dot'></span>old active-slave serial</span></div>"
    body = f"""
    {mode_banner}
    <section class="{cls}"><div class="{title_cls}">Status Meter</div>
      <div class="row"><span class="label">Active Slave</span><span class="badge up"><span class="dot"></span>Slave {active_slave}</span></div>
      <div class="row"><span class="label">Last Poll</span><span class="val">OK, 184 ms</span></div>
      <div class="row"><span class="label">Preset Aktif</span><span class="val">{preset_label(active_preset)}</span></div>
    </section>
    <section class="{cls}"><div class="{title_cls}">Test Koneksi (Ping Register)</div>
      <div style="font-size:12px;color:#64748b;margin-bottom:8px">Baca 1 register langsung - cek respon RTU tanpa nunggu polling penuh.</div>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px 16px">
        <div><label>Slave ID</label><input type="number" id="pingSlave" min="1" max="247" value="{active_slave}"></div>
        <div><label>Alamat Register (desimal)</label><input type="number" id="pingAddr" min="0" max="65535" value="16384"></div>
      </div>
      <button class="btn btn-sm" style="margin-top:8px" onclick="pingRegister()">Ping</button>
      <div id="pingResult" style="margin-top:8px;font-size:13px;font-family:monospace;white-space:pre-wrap">Ready.</div>
    </section>
    <section class="{cls}"><div class="{title_cls}">Koneksi RS485</div>{intro}
      <p class="muted">Serial bus strict dan global. Baudrate, parity, stop bits, timeout, dan retry harus sama untuk semua PM.</p>
      <label>Baud Rate</label><select id="baud"><option>1200</option><option>2400</option><option>4800</option><option {'selected' if cfg['baudrate']==9600 else ''}>9600</option><option {'selected' if cfg['baudrate']==19200 else ''}>19200</option><option {'selected' if cfg['baudrate']==38400 else ''}>38400</option><option>115200</option></select>
      <label>Parity</label><select id="parity"><option value="0" {'selected' if cfg['parity']==0 else ''}>Even</option><option value="2" {'selected' if cfg['parity']==2 else ''}>None</option></select>
      <label>Stop Bits</label><select id="stop"><option {'selected' if cfg['stop_bits']==1 else ''}>1</option><option {'selected' if cfg['stop_bits']==2 else ''}>2</option></select>
      <label>Poll Interval (ms)</label><input type="number" id="poll" min="200" max="60000" value="{cfg['poll_interval']}">
    </section>
    <section class="{cls}"><div class="{title_cls}">Step 1 - Jumlah PM</div>
      <label>Jumlah PM yang diambil</label><select id="pm_count" onchange="resizePmList()">{pm_count_options}</select>
      <p class="muted">Setelah jumlah dipilih, tiap urutan memilih preset register map. Serial bus tetap strict/global.</p>
    </section>
    <section class="{cls}"><div class="{title_cls}">Step 2 - Urutan Polling PM</div>
      <div id="pm_list">{device_rows}</div>
      <button class="btn wide" onclick="saveModbus()">Simpan Modbus</button><p id="mb_msg" class="muted">Ready.</p>
    </section>
    <section class="{cls}"><div class="{title_cls}">Old Active-Slave Strategy</div>
      <div class="row"><span class="label">Active Slave</span><span class="badge up"><span class="dot"></span>Slave {active_slave}</span></div>
      <div class="row"><span class="label">Template</span><span class="val">per-slot preset register map</span></div>
      <div class="row"><span class="label">Phase</span><span class="val">boleh campur 1P / 3P</span></div>
      <button class="btn btn-sm" onclick="nextSlave()">Next Slave</button>
      <p class="muted">Mengikuti pola old.bak: satu slave aktif dibaca penuh, register map/konversi dipilih dari preset slot, lalu cursor pindah ke urutan berikutnya.</p>
    </section>
    <section class="{cls}"><div class="{title_cls}">Multi-device Schedule</div>
      <table><tr><th>Urutan</th><th>Slave ID</th><th>Register Map</th><th>State</th></tr>{schedule_rows}</table>
      <div class="sep"></div>
      <div class="row"><span class="label">Interval</span><span class="val">{cfg['poll_interval']} ms</span></div>
      <div class="row"><span class="label">Timeout / Retry</span><span class="val">{cfg['timeout']} ms / {cfg['retry_count']}</span></div>
      <p class="muted">Semua slave PM1611 memakai serial strict yang sama; register map boleh mengikuti preset masing-masing slot.</p>
    </section>
    <script>
    async function saveModbus(){{
      const body = new URLSearchParams({{
        modbus_baudrate: document.getElementById('baud').value,
        modbus_parity: document.getElementById('parity').value,
        modbus_stop_bits: document.getElementById('stop').value,
        modbus_slave_ids: Array.from(document.querySelectorAll('.pm-slave')).map(x=>x.value).join(','),
        modbus_presets: Array.from(document.querySelectorAll('.pm-type')).map(x=>x.value).join(',')
      }});
      const d = await (await fetch('/api/modbus/save', {{method:'POST', body}})).json();
      document.getElementById('mb_msg').textContent = d.ok ? 'Saved in simulator.' : 'Save failed';
    }}
    function pingRegister(){{
      const slave=document.getElementById('pingSlave').value||'{active_slave}';
      const addr=document.getElementById('pingAddr').value||'16384';
      document.getElementById('pingResult').textContent='OK - slave='+slave+' addr='+addr+'\\nraw bytes: 43 5F 66 66\\nas FP32: 223.4';
    }}
    function resizePmList(){{
      const count=parseInt(document.getElementById('pm_count').value)||1;
      const list=document.getElementById('pm_list');
      const oldSlaves=Array.from(document.querySelectorAll('.pm-slave')).map(x=>x.value);
      const oldTypes=Array.from(document.querySelectorAll('.pm-type')).map(x=>x.value);
      let html='';
      for(let i=0;i<count;i++){{
        const sid=oldSlaves[i] || String(i+1);
        const typ=oldTypes[i] || 'PM1611_1PH';
        html += `<div class="card" style="box-shadow:none;border:1px solid #e2e8f0;padding:10px;margin-top:8px">
          <div class="card-title">Urutan PM ${{i+1}}</div>
          <label>Slave ID</label><input type="number" class="pm-slave" min="1" max="247" value="${{sid}}">
          <label>Preset Register Map</label><select class="pm-type">
            <option value="p1_1ph" ${{typ==='p1_1ph'?'selected':''}}>PM2230 / PM2120 / EM6400 - 1-Phase</option>
            <option value="p1_3ph" ${{typ==='p1_3ph'?'selected':''}}>PM2230 / EM6400 - 3-Phase</option>
            <option value="renata_3ph" ${{typ==='renata_3ph'?'selected':''}}>Renata AX9L - 3-Phase (INT32)</option>
          </select></div>`;
      }}
      list.innerHTML=html;
    }}
    async function nextSlave(){{
      await fetch('/api/modbus/next_slave', {{method:'POST'}});
      location.reload();
    }}
    </script>
    """
    return shell(prefix, "SEMS AIoT &mdash; Modbus" if prefix == "new" else "Modbus", body)


def simple_page(prefix: str, title: str) -> bytes:
    cls = "card" if prefix == "old" else "panel"
    title_cls = "k" if prefix == "old" else "title"
    return shell(prefix, title, f"<section class='{cls}'><div class='{title_cls}'>Simulator</div><p class='muted'>Placeholder page for layout checks.</p></section>")


class Handler(BaseHTTPRequestHandler):
    def _send(self, code: int, body: bytes, content_type: str = "text/html; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path.rstrip("/")
        if path == "":
            return self._send(200, compare_page())
        for prefix in ("old", "new"):
            if path == f"/{prefix}":
                return self._send(200, status_page(prefix))
            if path == f"/{prefix}/mqtt":
                return self._send(200, mqtt_old_page() if prefix == "old" else mqtt_new_page())
            if path == f"/{prefix}/modbus":
                return self._send(200, modbus_page(prefix))
            if path == f"/{prefix}/protection":
                return self._send(200, simple_page(prefix, "Relay / Protection"))
            if prefix == "old" and path == "/old/network":
                return self._send(200, simple_page(prefix, "SEMS AIoT &mdash; Network"))
            if prefix == "old" and path == "/old/update":
                return self._send(200, simple_page(prefix, "SEMS AIoT &mdash; OTA"))
        if path == "/api/status":
            payload = {"mode": state["mode"], "mqtt_connected": state["mqtt"]["connected"], "mqtt_base_topic": base_topic(), "relays": state["relays"], "meters": state["meters"], "uptime": int(time.monotonic())}
            return self._send(200, json.dumps(payload).encode(), "application/json")
        if path == "/api/mqtt/test_status":
            return self._send(200, json.dumps({"ok": True, "last_push": state["last_push"]}).encode(), "application/json")
        self._send(404, b"not found", "text/plain")

    def do_POST(self):
        path = urlparse(self.path).path.rstrip("/")
        size = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(size).decode("utf-8", errors="replace")
        form = parse_qs(raw)
        if path == "/api/mqtt/save":
            state["mqtt"]["enabled"] = form.get("mqtt_enabled", ["0"])[0] == "1"
            state["mqtt"]["host"] = form.get("mqtt_host", [state["mqtt"]["host"]])[0]
            state["mqtt"]["port"] = int(form.get("mqtt_port", [state["mqtt"]["port"]])[0] or 1883)
            state["mqtt"]["base_topic"] = form.get("mqtt_base_topic", [state["mqtt"]["base_topic"]])[0]
            state["mqtt"]["publish_interval"] = int(form.get("mqtt_publish_interval", [state["mqtt"]["publish_interval"]])[0] or 5)
            state["device_name"] = form.get("device_name", [state["device_name"]])[0]
            return self._send(200, b'{"ok":true}', "application/json")
        if path == "/api/mqtt/test_publish":
            state["last_push"] = time.strftime("%Y-%m-%d %H:%M:%S")
            return self._send(200, json.dumps({"ok": True, "topics": [f"{base_topic()}/control-state"]}).encode(), "application/json")
        if path == "/api/modbus/save":
            state["modbus"]["baudrate"] = int(form.get("modbus_baudrate", [state["modbus"]["baudrate"]])[0])
            state["modbus"]["parity"] = int(form.get("modbus_parity", [state["modbus"]["parity"]])[0])
            state["modbus"]["stop_bits"] = int(form.get("modbus_stop_bits", [state["modbus"]["stop_bits"]])[0])
            slaves = form.get("modbus_slave_ids", ["1,2,3"])[0]
            state["modbus"]["slave_ids"] = [int(x.strip()) for x in slaves.split(",") if x.strip().isdigit()][:4]
            presets = [x.strip() for x in form.get("modbus_presets", [""])[0].split(",") if x.strip()]
            allowed = set(PRESET_LABELS.keys())
            state["modbus"]["presets"] = [(p if p in allowed else "p1_1ph") for p in presets[:len(state["modbus"]["slave_ids"])]]
            while len(state["modbus"]["presets"]) < len(state["modbus"]["slave_ids"]):
                state["modbus"]["presets"].append("p1_1ph")
            state["modbus"]["active_index"] %= max(1, len(state["modbus"]["slave_ids"]))
            return self._send(200, b'{"ok":true}', "application/json")
        if path == "/api/modbus/next_slave":
            ids = state["modbus"]["slave_ids"]
            state["modbus"]["active_index"] = (state["modbus"]["active_index"] + 1) % max(1, len(ids))
            return self._send(200, b'{"ok":true}', "application/json")
        if path == "/api/mode/toggle":
            state["mode"] = "Normal" if state["mode"] == "Config" else "Config"
            return self._send(200, b'{"ok":true}', "application/json")
        if path == "/api/mode/set":
            mode = form.get("mode", [state["mode"]])[0]
            state["mode"] = "Config" if mode == "Config" else "Normal"
            return self._send(200, b'{"ok":true}', "application/json")
        self._send(404, b"not found", "text/plain")


def main():
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"SEMS Web UI simulator running: http://127.0.0.1:{PORT}")
    print("Compare old/new:")
    print(f"  http://127.0.0.1:{PORT}/old/")
    print(f"  http://127.0.0.1:{PORT}/new/")
    server.serve_forever()


if __name__ == "__main__":
    main()
