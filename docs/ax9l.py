from pymodbus.client import ModbusSerialClient
import time
import sys
import struct
import json
import paho.mqtt.client as mqtt
from datetime import datetime
import platform
import os
import csv
import threading
import logging
from flask import Flask, jsonify, request, render_template_string

# --- KONFIGURASI REGISTER METERS (AX9L) ---
# Format: (Nama Variabel, Alamat Register, Pengali, Unit, Deskripsi)
REGISTERS_MAP = [
    ("UA", 0x4000, 0.1, "V", "Phase voltage A"),
    ("UB", 0x4002, 0.1, "V", "Phase voltage B"),
    ("UC", 0x4004, 0.1, "V", "Phase voltage C"),
    ("UAB", 0x4006, 0.1, "V", "Line voltage AB"),
    ("UBC", 0x4008, 0.1, "V", "Line voltage BC"),
    ("UCA", 0x400A, 0.1, "V", "Line voltage CA"),
    ("IA", 0x400C, 0.001, "A", "Phase current A"),
    ("IB", 0x400E, 0.001, "A", "Phase current B"),
    ("IC", 0x4010, 0.001, "A", "Phase current C"),
    ("PA", 0x4012, 0.1, "W", "Active power A"),
    ("PB", 0x4014, 0.1, "W", "Active power B"),
    ("PC", 0x4016, 0.1, "W", "Active power C"),
    ("P_Total", 0x4018, 0.1, "W", "Total active power"),
    ("QA", 0x401A, 0.1, "var", "Reactive power A"),
    ("QB", 0x401C, 0.1, "var", "Reactive power B"),
    ("QC", 0x401E, 0.1, "var", "Reactive power C"),
    ("Q_Total", 0x4020, 0.1, "var", "Total reactive power"),
    ("SA", 0x4022, 0.1, "VA", "Apparent power A"),
    ("SB", 0x4024, 0.1, "VA", "Apparent power B"),
    ("SC", 0x4026, 0.1, "VA", "Apparent power C"),
    ("S_Total", 0x4028, 0.1, "VA", "Total apparent power"),
    ("PF1", 0x402A, 0.001, "", "Power factor A"),
    ("PF2", 0x402C, 0.001, "", "Power factor B"),
    ("PF3", 0x402E, 0.001, "", "Power factor C"),
    ("PF_Avg", 0x4030, 0.001, "", "Average power factor"),
    ("Freq", 0x4032, 0.01, "Hz", "Frequency"),
    ("kWh_Total", 0x4034, 0.01, "kWh", "Total Active Energy"),
    ("kvarh_Total", 0x4036, 0.01, "kvarh", "Total Reactive Energy"),
    ("kWh_Forward", 0x4038, 0.01, "kWh", "Forward Active Energy"),
    ("kWh_Backward", 0x403A, 0.01, "kWh", "Backward Active Energy"),
    ("kvarh_Forward", 0x403C, 0.01, "kvarh", "Forward Reactive Energy"),
    ("kvarh_Backward", 0x403E, 0.01, "kvarh", "Backward Reactive Energy")
]

START_ADDRESS = 0x4000
COUNT = 64

def decode_register_32(registers, start_address, target_address, wordorder='big'):
    """
    Mendekode 2 buah register 16-bit menjadi satu integer 32-bit (signed).
    """
    idx = target_address - start_address
    if idx < 0 or idx + 1 >= len(registers):
        raise ValueError(f"Alamat register {hex(target_address)} (desimal: {target_address}) di luar jangkauan data yang dibaca.")
    
    r0 = registers[idx]
    r1 = registers[idx + 1]
    
    # Konversi ke bytes (big-endian 16-bit word)
    b0 = struct.pack('>H', r0)
    b1 = struct.pack('>H', r1)
    
    # Gabungkan sesuai word order
    if wordorder.lower() == 'little':
        bytes_32 = b1 + b0
    else:
        bytes_32 = b0 + b1
        
    return struct.unpack('>i', bytes_32)[0]

def safe_read_holding_registers(client, address, count, slave_id):
    """
    Membaca holding registers dengan mencari keyword parameter yang cocok (device_id, slave, atau unit).
    """
    for param_name in ['device_id', 'slave', 'unit']:
        try:
            kwargs = {
                'address': address,
                'count': count,
                param_name: slave_id
            }
            return client.read_holding_registers(**kwargs)
        except TypeError:
            continue
    raise TypeError("Fungsi read_holding_registers tidak mendukung device_id, slave, maupun unit pada versi pymodbus ini.")

def format_decimal_string(val, decimals):
    """
    Mengonversi nilai menjadi string dengan jumlah desimal yang ditentukan.
    """
    try:
        return f"{float(val):.{decimals}f}"
    except (ValueError, TypeError):
        return "0." + ("0" * decimals)

# SETTING PARAMETER & CONFIGURATION ---------------------------------------------

# --- CONFIGURATION DEFAULT & JSON CONFIG MANAGER ---
DEFAULT_CONFIG = {
    "SERIAL_PORT": "COM3" if platform.system() == "Windows" else "/dev/ttyUSB1",
    "BAUDRATE": 9600,
    "SLAVE_IDS": [1, 2, 3],
    "SLAVE_LABELS": {},
    "MQTT_BROKER": "clowsens.cloud",
    "MQTT_PORT": 1883,
    "MQTT_USERNAME": "",
    "MQTT_PASSWORD": "",
    "MQTT_TOPIC_ENERGY": "trofis/enms/unsoed-rs/energy/slave_{slave_id}",
    "MQTT_TOPIC_KWH": "trofis/enms/unsoed-rs/kwh/slave_{slave_id}",
    "SEND_INTERVAL_SECONDS": 300,
    "SAMPLE_COUNT": 20,
    "LOGGER_ID": "0001",
    "WEB_PORT": 5000
}

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(SCRIPT_DIR, "config.json")
active_config = {}

# Global configuration variables to update dynamically
PORT = ""
BAUDRATE = 9600
SLAVE_IDS = []
MQTT_BROKER = ""
MQTT_PORT = 1883
MQTT_USERNAME = ""
MQTT_PASSWORD = ""
MQTT_TOPIC_ENERGY = ""
MQTT_TOPIC_KWH = ""
SEND_INTERVAL_SECONDS = 300
SAMPLE_COUNT = 20
LOGGER_ID = ""
SAMPLE_DELAY = 15.0
FILENAME_CSV = ""

def get_aligned_timestamp(dt, interval_seconds):
    ts = dt.timestamp()
    aligned_ts = round(ts / interval_seconds) * interval_seconds
    aligned_dt = datetime.fromtimestamp(aligned_ts)
    return aligned_dt.strftime("%Y-%m-%d %H:%M:00")

def log_print(message):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{timestamp}] {message}")

def load_config():
    global active_config
    global PORT, BAUDRATE, SLAVE_IDS, MQTT_BROKER, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD
    global MQTT_TOPIC_ENERGY, MQTT_TOPIC_KWH, SEND_INTERVAL_SECONDS, SAMPLE_COUNT
    global LOGGER_ID, SAMPLE_DELAY, FILENAME_CSV
    
    if not os.path.exists(CONFIG_FILE):
        active_config = DEFAULT_CONFIG.copy()
        try:
            with open(CONFIG_FILE, "w") as f:
                json.dump(active_config, f, indent=4)
            log_print(f"File konfigurasi baru dibuat: {CONFIG_FILE}")
        except Exception as e:
            log_print(f"Gagal membuat config.json default: {e}")
    else:
        try:
            with open(CONFIG_FILE, "r") as f:
                loaded = json.load(f)
                active_config = DEFAULT_CONFIG.copy()
                active_config.update(loaded)
        except Exception as e:
            log_print(f"Gagal membaca config.json, menggunakan default: {e}")
            active_config = DEFAULT_CONFIG.copy()
            
    # Update global variables
    PORT = active_config["SERIAL_PORT"]
    BAUDRATE = active_config["BAUDRATE"]
    SLAVE_IDS = active_config["SLAVE_IDS"]
    MQTT_BROKER = active_config["MQTT_BROKER"]
    MQTT_PORT = active_config["MQTT_PORT"]
    MQTT_USERNAME = active_config.get("MQTT_USERNAME", "")
    MQTT_PASSWORD = active_config.get("MQTT_PASSWORD", "")
    MQTT_TOPIC_ENERGY = active_config["MQTT_TOPIC_ENERGY"]
    MQTT_TOPIC_KWH = active_config["MQTT_TOPIC_KWH"]
    SEND_INTERVAL_SECONDS = active_config["SEND_INTERVAL_SECONDS"]
    SAMPLE_COUNT = active_config["SAMPLE_COUNT"]
    LOGGER_ID = active_config["LOGGER_ID"]
    
    SAMPLE_DELAY = SEND_INTERVAL_SECONDS / max(1, SAMPLE_COUNT)
    FILENAME_CSV = os.path.join(SCRIPT_DIR, f"{LOGGER_ID}_ENERGY-DATA.csv")
    log_print(f"Konfigurasi dimuat. Port: {PORT}, MQTT Broker: {MQTT_BROKER}, Interval: {SEND_INTERVAL_SECONDS}s, Sample Count: {SAMPLE_COUNT}")

def save_config(new_config):
    try:
        with open(CONFIG_FILE, "w") as f:
            json.dump(new_config, f, indent=4)
        load_config()
        config_changed_event.set()
        return True
    except Exception as e:
        print(f"Gagal menyimpan config.json: {e}")
        return False

# Initialize Config on startup
load_config()

# --- STATUS KONEKSI & GLOBAL STATE (THREAD-SAFE) ---
config_changed_event = threading.Event()
state_lock = threading.Lock()
shared_state = {
    "modbus_connected": False,
    "mqtt_connected": False,
    "slave_status": {},  # slave_id -> {"status": "Success"/"Failed", "timestamp": "..."}
    "latest_data": {},    # slave_id -> dict of sensor values
    "mqtt_status": {}     # slave_id -> {"status": "Success"/"Failed", "timestamp": "..."}
}

# Status Koneksi MQTT Global
is_mqtt_connected = False

# --- WEB SERVER EMBEDDED (FLASK - ESPHOME STYLE) ---
web_app = Flask(__name__)

# Suppress Flask request logs to keep stdout clean
log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)

INDEX_HTML = """<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ENMS RS UNSOED - AX9L Logger</title>
    <style>
        :root {
            --bg-color: #09090b;
            --text-color: #f4f4f5;
            --card-bg: #18181b;
            --border-color: #27272a;
            --accent-color: #0ea5e9;
            --success-color: #22c55e;
            --error-color: #ef4444;
            --text-secondary: #a1a1aa;
            --shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.5), 0 2px 4px -1px rgba(0, 0, 0, 0.3);
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            margin: 0;
            padding: 20px;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .container {
            width: 100%;
            max-width: 1200px;
        }
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 2px solid var(--border-color);
            padding-bottom: 10px;
            margin-bottom: 20px;
        }
        h1, h2, h3 {
            margin: 0;
            font-weight: 500;
        }
        h1 { font-size: 24px; }
        h2 { font-size: 18px; margin-bottom: 15px; border-bottom: 1px solid var(--border-color); padding-bottom: 5px; }
        .status-container {
            display: flex;
            gap: 20px;
            flex-wrap: wrap;
            margin-bottom: 20px;
        }
        .status-card {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 15px 20px;
            flex: 1;
            min-width: 250px;
            box-shadow: var(--shadow);
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        .status-info {
            display: flex;
            flex-direction: column;
        }
        .status-label {
            font-size: 12px;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .status-value {
            font-size: 16px;
            font-weight: bold;
            margin-top: 5px;
        }
        .status-detail {
            font-size: 12px;
            color: var(--text-secondary);
            margin-top: 3px;
        }
        .badge {
            display: inline-flex;
            align-items: center;
            padding: 4px 8px;
            border-radius: 12px;
            font-size: 12px;
            font-weight: bold;
            text-transform: uppercase;
        }
        .badge-success {
            background-color: rgba(76, 175, 80, 0.15);
            color: var(--success-color);
        }
        .badge-danger {
            background-color: rgba(244, 67, 54, 0.15);
            color: var(--error-color);
        }
        .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            margin-right: 6px;
            display: inline-block;
        }
        .dot-success { background-color: var(--success-color); box-shadow: 0 0 8px var(--success-color); }
        .dot-danger { background-color: var(--error-color); box-shadow: 0 0 8px var(--error-color); }
        
        .meters-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(340px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .meter-card {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 20px;
            box-shadow: var(--shadow);
        }
        .meter-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 15px;
        }
        .sensor-table {
            width: 100%;
            border-collapse: collapse;
        }
        .sensor-table td {
            border: 1px solid var(--border-color);
            padding: 6px 8px;
            font-size: 13px;
            text-align: center;
        }
        .table-label {
            font-weight: bold;
            text-align: left;
            padding-left: 10px;
            background-color: rgba(255, 255, 255, 0.03);
            width: 130px;
        }
        .sensor-value {
            font-weight: bold;
            font-family: "Courier New", Courier, monospace;
        }
        .settings-card {
            background-color: var(--card-bg);
            border: 1px solid var(--border-color);
            border-radius: 8px;
            padding: 20px;
            box-shadow: var(--shadow);
            margin-bottom: 30px;
        }
        .form-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 15px 20px;
            margin-bottom: 20px;
        }
        .form-group {
            display: flex;
            flex-direction: column;
        }
        .form-group-full {
            grid-column: 1 / -1;
        }
        label {
            font-size: 13px;
            color: var(--text-secondary);
            margin-bottom: 5px;
            font-weight: bold;
        }
        input, select {
            background-color: var(--bg-color);
            color: var(--text-color);
            border: 1px solid var(--border-color);
            border-radius: 4px;
            padding: 8px 10px;
            font-size: 14px;
            outline: none;
            transition: border-color 0.2s;
        }
        input:focus, select:focus {
            border-color: var(--accent-color);
        }
        .password-container {
            position: relative;
            display: flex;
            align-items: center;
        }
        .password-container input {
            width: 100%;
            padding-right: 35px;
        }
        .toggle-password {
            position: absolute;
            right: 10px;
            cursor: pointer;
            color: var(--text-secondary);
            user-select: none;
            font-size: 12px;
        }
        .btn {
            background-color: var(--accent-color);
            color: white;
            border: none;
            border-radius: 4px;
            padding: 10px 20px;
            font-size: 14px;
            font-weight: bold;
            cursor: pointer;
            transition: background-color 0.2s, transform 0.1s;
            align-self: flex-start;
        }
        .btn:hover {
            background-color: #0288d1;
        }
        .btn:active {
            transform: scale(0.98);
        }
        .toast {
            position: fixed;
            bottom: 20px;
            right: 20px;
            background-color: #323232;
            color: white;
            padding: 12px 24px;
            border-radius: 4px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
            opacity: 0;
            transition: opacity 0.3s, transform 0.3s;
            transform: translateY(20px);
            z-index: 1000;
            font-size: 14px;
        }
        .toast.show {
            opacity: 1;
            transform: translateY(0);
        }
        .toast-success { border-left: 4px solid var(--success-color); }
        .toast-error { border-left: 4px solid var(--error-color); }
        .footer {
            text-align: center;
            padding: 20px 0;
            font-size: 12px;
            color: var(--text-secondary);
            border-top: 1px solid var(--border-color);
            width: 100%;
        }
        .footer a {
            color: var(--text-secondary);
            text-decoration: none;
            transition: color 0.2s;
        }
        .footer a:hover {
            color: var(--accent-color);
            text-decoration: underline;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div>
                <h1>ENMS RS UNSOED - AX9L</h1>
                <div style="font-size: 12px; color: var(--text-secondary);">SIMONELIS-RS Web Server</div>
            </div>
            <button class="btn" onclick="toggleSettings()">⚙️ Configuration</button>
        </header>

        <div class="status-container">
            <div class="status-card">
                <div class="status-info">
                    <div class="status-label">Modbus RTU Connection</div>
                    <div class="status-value" id="modbus-state">-</div>
                    <div class="status-detail" id="modbus-details">-</div>
                </div>
                <div id="modbus-badge">-</div>
            </div>
            
            <div class="status-card">
                <div class="status-info">
                    <div class="status-label">MQTT Connection</div>
                    <div class="status-value" id="mqtt-state">-</div>
                    <div class="status-detail" id="mqtt-details">-</div>
                </div>
                <div id="mqtt-badge">-</div>
            </div>
        </div>

        <h2>States</h2>
        <div class="meters-grid" id="meters-grid">
            <div style="grid-column: 1/-1; text-align: center; padding: 20px; color: var(--text-secondary);">
                Menunggu data pembacaan sensor...
            </div>
        </div>

        <div id="settings-container" style="display: none; margin-top: 20px;">
            <h2>Configuration Parameters</h2>
            <div class="settings-card" style="position: relative; min-height: 250px;">
                <!-- Lock Overlay -->
                <div id="settings-lock-overlay" style="position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: rgba(24, 24, 27, 0.98); display: flex; flex-direction: column; align-items: center; justify-content: center; z-index: 10; border-radius: 8px; padding: 20px; text-align: center; border: 1px solid var(--border-color);">
                    <div style="font-size: 40px; margin-bottom: 10px;">🔒</div>
                    <h3 style="margin-bottom: 8px; color: var(--text-color); font-size: 16px;">Konfigurasi Terkunci</h3>
                    <p style="color: var(--text-secondary); font-size: 12px; max-width: 300px; margin-bottom: 15px; line-height: 1.5;">Masukkan PIN untuk memverifikasi hak akses dan memuat data konfigurasi logger AX9L.</p>
                    <button type="button" class="btn" onclick="unlockConfiguration()" style="padding: 8px 16px; font-size: 13px;">🔓 Unlock Settings</button>
                </div>
                
                <div id="settings-unlocked-header" style="display: none; justify-content: space-between; align-items: center; margin-bottom: 20px; padding-bottom: 15px; border-bottom: 1px dashed var(--border-color); flex-wrap: wrap; gap: 10px;">
                    <div style="font-size: 13px; font-weight: bold; color: var(--success-color);">
                        🔓 Status: Terbuka (Siap Diedit)
                    </div>
                    <button type="button" class="btn" onclick="lockConfiguration()" style="padding: 6px 12px; font-size: 12px; background-color: var(--error-color);">🔒 Lock Settings</button>
                </div>
                
                <form id="settings-form">
                    <div class="form-grid">
                        <div class="form-group">
                            <label for="SERIAL_PORT">Serial Port</label>
                            <input type="text" id="SERIAL_PORT" required disabled>
                        </div>
                        <div class="form-group">
                            <label for="BAUDRATE">Baudrate</label>
                            <select id="BAUDRATE" disabled>
                                <option value="4800">4800</option>
                                <option value="9600" selected>9600</option>
                                <option value="19200">19200</option>
                                <option value="38400">38400</option>
                                <option value="115200">115200</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label for="SLAVE_IDS">Slave IDs (koma sebagai pemisah)</label>
                            <input type="text" id="SLAVE_IDS" placeholder="e.g. 1, 2, 3" required disabled>
                        </div>
                        <div class="form-group">
                            <label for="WEB_PORT">Web Server Port</label>
                            <input type="number" id="WEB_PORT" min="1" max="65535" required disabled>
                        </div>
                        <div class="form-group">
                            <label for="SEND_INTERVAL_SECONDS">Interval Log (Menit)</label>
                            <input type="number" id="SEND_INTERVAL_SECONDS" min="1" required disabled>
                        </div>
                        <div class="form-group">
                            <label for="SAMPLE_COUNT">Jumlah Baca (Sampel)</label>
                            <input type="number" id="SAMPLE_COUNT" min="1" required disabled>
                        </div>
                        <div class="form-group">
                            <label for="MQTT_BROKER">MQTT Broker</label>
                            <input type="text" id="MQTT_BROKER" required disabled>
                        </div>
                        <div class="form-group">
                            <label for="MQTT_PORT">MQTT Port</label>
                            <input type="number" id="MQTT_PORT" min="1" max="65535" required disabled>
                        </div>
                        <div class="form-group">
                            <label for="MQTT_USERNAME">MQTT Username</label>
                            <input type="text" id="MQTT_USERNAME" placeholder="Kosongkan jika tanpa auth" disabled>
                        </div>
                        <div class="form-group">
                            <label for="MQTT_PASSWORD">MQTT Password</label>
                            <div class="password-container">
                                <input type="password" id="MQTT_PASSWORD" placeholder="Kosongkan jika tanpa auth" disabled>
                                <span class="toggle-password" onclick="togglePasswordVisibility()">Show</span>
                            </div>
                        </div>
                        <div class="form-group form-group-full">
                            <label for="MQTT_TOPIC_ENERGY">MQTT Topic Energy (gunakan {slave_id})</label>
                            <input type="text" id="MQTT_TOPIC_ENERGY" required style="font-family: monospace;" disabled>
                        </div>
                        <div class="form-group form-group-full">
                            <label for="MQTT_TOPIC_KWH">MQTT Topic KWH (gunakan {slave_id})</label>
                            <input type="text" id="MQTT_TOPIC_KWH" required style="font-family: monospace;" disabled>
                        </div>
                    </div>
                    
                    <div id="slave-labels-container" class="form-grid" style="margin-top: 15px; border-top: 1px dashed var(--border-color); padding-top: 15px;">
                    </div>
                    
                    <button type="submit" class="btn" id="save-btn" style="margin-top: 20px;" disabled>Save Settings</button>
                </form>
            </div>
        </div>

        <div class="footer">
            &copy; 2026 <a href="https://trofis.tech/" target="_blank" rel="noopener noreferrer">TROFIS.TECH</a>
        </div>
    </div>

    <div class="toast" id="toast"></div>

    <script>
        let isConfigLoaded = false;
        let isUnlocked = false;
        let configPIN = '';
        let currentSlaveLabels = {};
        let activeConfigCache = null;

        function showToast(message, isSuccess = true) {
            const toast = document.getElementById('toast');
            toast.textContent = message;
            toast.className = 'toast show ' + (isSuccess ? 'toast-success' : 'toast-error');
            setTimeout(() => {
                toast.className = 'toast';
            }, 3000);
        }

        function togglePasswordVisibility() {
            const passwordInput = document.getElementById('MQTT_PASSWORD');
            const toggleText = document.querySelector('.toggle-password');
            if (passwordInput.type === 'password') {
                passwordInput.type = 'text';
                toggleText.textContent = 'Hide';
            } else {
                passwordInput.type = 'password';
                toggleText.textContent = 'Show';
            }
        }

        function toggleFormInputs(enabled) {
            const form = document.getElementById('settings-form');
            const inputs = form.querySelectorAll('input, select, button[type="submit"]');
            inputs.forEach(input => {
                input.disabled = !enabled;
            });
            const slaveInputs = document.querySelectorAll('.slave-label-input');
            slaveInputs.forEach(input => {
                input.disabled = !enabled;
            });
        }

        function unlockConfiguration() {
            const pin = prompt("Masukkan PIN/Password Konfigurasi:");
            if (pin === "1234") {
                fetch(`/api/config?pin=${pin}`)
                    .then(response => response.json())
                    .then(res => {
                        if (res.status === 'success') {
                            isUnlocked = true;
                            configPIN = pin;
                            activeConfigCache = res.config;
                            
                            document.getElementById('settings-lock-overlay').style.display = 'none';
                            document.getElementById('settings-unlocked-header').style.display = 'flex';
                            
                            isConfigLoaded = false;
                            populateForm(res.config);
                            
                            showToast("Konfigurasi berhasil dibuka!", true);
                            
                            document.getElementById('modbus-details').textContent = `Port: ${res.config.SERIAL_PORT} | Baudrate: ${res.config.BAUDRATE}`;
                            document.getElementById('mqtt-details').textContent = `Broker: ${res.config.MQTT_BROKER}:${res.config.MQTT_PORT}`;
                        } else {
                            showToast(res.message, false);
                        }
                    })
                    .catch(error => {
                        console.error('Error fetching config:', error);
                        showToast('Gagal memuat data konfigurasi.', false);
                    });
            } else if (pin !== null) {
                showToast("PIN salah!", false);
            }
        }

        function lockConfiguration() {
            isUnlocked = false;
            configPIN = '';
            activeConfigCache = null;
            
            document.getElementById('settings-lock-overlay').style.display = 'flex';
            document.getElementById('settings-unlocked-header').style.display = 'none';
            
            toggleFormInputs(false);
            
            document.getElementById('SERIAL_PORT').value = '';
            document.getElementById('BAUDRATE').value = '9600';
            document.getElementById('SLAVE_IDS').value = '';
            document.getElementById('WEB_PORT').value = '';
            document.getElementById('SEND_INTERVAL_SECONDS').value = '';
            document.getElementById('SAMPLE_COUNT').value = '';
            document.getElementById('MQTT_BROKER').value = '';
            document.getElementById('MQTT_PORT').value = '';
            document.getElementById('MQTT_USERNAME').value = '';
            document.getElementById('MQTT_PASSWORD').value = '';
            document.getElementById('MQTT_TOPIC_ENERGY').value = '';
            document.getElementById('MQTT_TOPIC_KWH').value = '';
            
            const container = document.getElementById('slave-labels-container');
            if (container) container.innerHTML = '';
            
            document.getElementById('modbus-details').textContent = `Port: (Terproteksi) | Baudrate: (Terproteksi)`;
            document.getElementById('mqtt-details').textContent = `Broker: (Terproteksi)`;
            
            isConfigLoaded = false;
            showToast("Konfigurasi dikunci kembali.", true);
        }

        function renderSlaveLabelInputs() {
            const container = document.getElementById('slave-labels-container');
            if (!container) return;
            container.innerHTML = '';
            
            const slaveIdsStr = document.getElementById('SLAVE_IDS').value;
            const slaveIds = slaveIdsStr.split(',')
                .map(x => x.trim())
                .filter(x => x.length > 0 && !isNaN(x))
                .map(x => parseInt(x));
                
            if (slaveIds.length === 0) return;
            
            const title = document.createElement('h3');
            title.textContent = 'Sub Judul / Nama Slave ID';
            title.style.gridColumn = '1 / -1';
            title.style.fontSize = '14px';
            title.style.marginTop = '10px';
            title.style.marginBottom = '5px';
            title.style.color = 'var(--text-secondary)';
            container.appendChild(title);
            
            slaveIds.forEach(id => {
                const labelVal = currentSlaveLabels[id] || '';
                const group = document.createElement('div');
                group.className = 'form-group';
                group.innerHTML = `
                    <label for="LABEL_SLAVE_${id}" style="font-size: 12px; color: var(--accent-color);">Sub Judul Slave ${id}</label>
                    <input type="text" class="slave-label-input" id="LABEL_SLAVE_${id}" data-slave-id="${id}" value="${labelVal}" placeholder="Misal: PANEL PENERANGAN" ${isUnlocked ? '' : 'disabled'}>
                `;
                container.appendChild(group);
            });
        }

        function updateStatusCards(data) {
            const modbusState = document.getElementById('modbus-state');
            const modbusDetails = document.getElementById('modbus-details');
            const modbusBadge = document.getElementById('modbus-badge');
            
            if (data.modbus_connected) {
                modbusState.textContent = 'Connected';
                modbusBadge.innerHTML = '<span class="badge badge-success"><span class="dot dot-success"></span>Connected</span>';
            } else {
                modbusState.textContent = 'Disconnected';
                modbusBadge.innerHTML = '<span class="badge badge-danger"><span class="dot dot-danger"></span>Disconnected</span>';
            }
            
            if (isUnlocked && activeConfigCache) {
                modbusDetails.textContent = `Port: ${activeConfigCache.SERIAL_PORT} | Baudrate: ${activeConfigCache.BAUDRATE}`;
            } else {
                modbusDetails.textContent = `Port: (Terproteksi) | Baudrate: (Terproteksi)`;
            }

            const mqttState = document.getElementById('mqtt-state');
            const mqttDetails = document.getElementById('mqtt-details');
            const mqttBadge = document.getElementById('mqtt-badge');
            
            if (data.mqtt_connected) {
                mqttState.textContent = 'Connected';
                mqttBadge.innerHTML = '<span class="badge badge-success"><span class="dot dot-success"></span>Connected</span>';
            } else {
                mqttState.textContent = 'Disconnected';
                mqttBadge.innerHTML = '<span class="badge badge-danger"><span class="dot dot-danger"></span>Disconnected</span>';
            }
            
            if (isUnlocked && activeConfigCache) {
                mqttDetails.textContent = `Broker: ${activeConfigCache.MQTT_BROKER}:${activeConfigCache.MQTT_PORT}`;
            } else {
                mqttDetails.textContent = `Broker: (Terproteksi)`;
            }
        }

        function toggleSettings() {
            const container = document.getElementById('settings-container');
            if (container.style.display === 'none') {
                container.style.display = 'block';
                container.scrollIntoView({ behavior: 'smooth' });
            } else {
                container.style.display = 'none';
            }
        }

        function populateForm(config) {
            if (!isUnlocked) return;
            if (isConfigLoaded) return;
            
            currentSlaveLabels = config.SLAVE_LABELS || {};
            
            document.getElementById('SERIAL_PORT').value = config.SERIAL_PORT;
            document.getElementById('BAUDRATE').value = config.BAUDRATE;
            document.getElementById('SLAVE_IDS').value = config.SLAVE_IDS.join(', ');
            document.getElementById('WEB_PORT').value = config.WEB_PORT;
            document.getElementById('SEND_INTERVAL_SECONDS').value = Math.round(config.SEND_INTERVAL_SECONDS / 60);
            document.getElementById('SAMPLE_COUNT').value = config.SAMPLE_COUNT;
            document.getElementById('MQTT_BROKER').value = config.MQTT_BROKER;
            document.getElementById('MQTT_PORT').value = config.MQTT_PORT;
            document.getElementById('MQTT_USERNAME').value = config.MQTT_USERNAME || '';
            document.getElementById('MQTT_PASSWORD').value = config.MQTT_PASSWORD_SET ? '********' : '';
            document.getElementById('MQTT_TOPIC_ENERGY').value = config.MQTT_TOPIC_ENERGY;
            document.getElementById('MQTT_TOPIC_KWH').value = config.MQTT_TOPIC_KWH;
            
            renderSlaveLabelInputs();
            toggleFormInputs(true);
            
            isConfigLoaded = true;
        }

        function generateMeterCards(data) {
            const grid = document.getElementById('meters-grid');
            const slaveIds = data.config.SLAVE_IDS;
            const latestData = data.latest_data || {};
            const slaveStatus = data.slave_status || {};
            const mqttStatus = data.mqtt_status || {};
            const slaveLabels = data.config.SLAVE_LABELS || {};
            
            if (slaveIds.length === 0) {
                grid.innerHTML = '<div style="grid-column: 1/-1; text-align: center; padding: 20px; color: var(--text-secondary);">Belum ada Slave ID yang dikonfigurasi.</div>';
                return;
            }

            let html = '';
            slaveIds.forEach(id => {
                const slaveLabel = slaveLabels[id] || '';
                const statusInfo = slaveStatus[id] || { status: 'Unknown', timestamp: '-' };
                const isSuccess = statusInfo.status === 'Success';
                const statusDot = isSuccess ? '<span class="dot dot-success"></span>' : '<span class="dot dot-danger"></span>';
                const statusText = isSuccess ? 'Success' : (statusInfo.status === 'Unknown' ? 'No Data' : 'Error');
                const lastReadTime = statusInfo.timestamp;
                
                const mqttInfo = mqttStatus[id] || { status: 'Unknown', timestamp: '-' };
                const isMqttSuccess = mqttInfo.status === 'Success';
                const mqttStatusText = isMqttSuccess ? 'Success' : (mqttInfo.status === 'Unknown' ? 'No Data' : 'Failed');
                const lastMqttTime = mqttInfo.timestamp;
                
                const meterData = latestData[id] || {};
                
                const format = (val, dec = 1) => {
                    if (val === undefined || val === null) return '-';
                    return Number(val).toFixed(dec);
                };

                html += `
                <div class="meter-card">
                    <div class="meter-header" style="align-items: flex-start;">
                        <div>
                            ${slaveLabel ? `<div style="font-size: 14px; font-weight: bold; color: var(--accent-color); text-transform: uppercase; margin-bottom: 2px;">${slaveLabel}</div>` : ''}
                            <h3 style="font-size: 12px; color: var(--text-secondary); font-weight: normal; margin-top: 2px;">slave ID: ${id}</h3>
                        </div>
                        <span class="badge ${isSuccess ? 'badge-success' : 'badge-danger'}">
                            ${statusDot}${statusText}
                        </span>
                    </div>
                    <table class="sensor-table">
                        <tr>
                            <td rowspan="2" class="table-label">VOLTAGE PHASE</td>
                            <td>VA</td>
                            <td>VB</td>
                            <td>VC</td>
                        </tr>
                        <tr>
                            <td class="sensor-value">${format(meterData.UA)} V</td>
                            <td class="sensor-value">${format(meterData.UB)} V</td>
                            <td class="sensor-value">${format(meterData.UC)} V</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">VOLTAGE LINE</td>
                            <td>VAB</td>
                            <td>VBC</td>
                            <td>VCA</td>
                        </tr>
                        <tr>
                            <td class="sensor-value">${format(meterData.UAB)} V</td>
                            <td class="sensor-value">${format(meterData.UBC)} V</td>
                            <td class="sensor-value">${format(meterData.UCA)} V</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">CURRENT</td>
                            <td>IA</td>
                            <td>IB</td>
                            <td>IC</td>
                        </tr>
                        <tr>
                            <td class="sensor-value">${format(meterData.IA, 3)} A</td>
                            <td class="sensor-value">${format(meterData.IB, 3)} A</td>
                            <td class="sensor-value">${format(meterData.IC, 3)} A</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">ACTIVE POWER</td>
                            <td>PA</td>
                            <td>PB</td>
                            <td>PC</td>
                        </tr>
                        <tr>
                            <td class="sensor-value">${format(meterData.PA)} W</td>
                            <td class="sensor-value">${format(meterData.PB)} W</td>
                            <td class="sensor-value">${format(meterData.PC)} W</td>
                        </tr>
                        <tr>
                            <td class="table-label">Ptotal</td>
                            <td colspan="3" class="sensor-value">${format(meterData.P_Total)} W</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">REACTIVE POWER</td>
                            <td>QA</td>
                            <td>QB</td>
                            <td>QC</td>
                        </tr>
                        <tr>
                            <td class="sensor-value">${format(meterData.QA)} var</td>
                            <td class="sensor-value">${format(meterData.QB)} var</td>
                            <td class="sensor-value">${format(meterData.QC)} var</td>
                        </tr>
                        <tr>
                            <td class="table-label">Qtotal</td>
                            <td colspan="3" class="sensor-value">${format(meterData.Q_Total)} var</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">Apparent Power</td>
                            <td>SA</td>
                            <td>SB</td>
                            <td>SC</td>
                        </tr>
                        <tr>
                            <td class="sensor-value">${format(meterData.SA)} VA</td>
                            <td class="sensor-value">${format(meterData.SB)} VA</td>
                            <td class="sensor-value">${format(meterData.SC)} VA</td>
                        </tr>
                        <tr>
                            <td class="table-label">Stotal</td>
                            <td colspan="3" class="sensor-value">${format(meterData.S_Total)} VA</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">POWER FACTOR</td>
                            <td>pFA</td>
                            <td>pFB</td>
                            <td>pFC</td>
                        </tr>
                        <tr>
                            <td class="sensor-value">${format(meterData.PF1, 3)}</td>
                            <td class="sensor-value">${format(meterData.PF2, 3)}</td>
                            <td class="sensor-value">${format(meterData.PF3, 3)}</td>
                        </tr>
                        <tr>
                            <td class="table-label">pF Avg</td>
                            <td colspan="3" class="sensor-value">${format(meterData.PF_Avg, 3)}</td>
                        </tr>
                        <tr>
                            <td class="table-label">Frequency</td>
                            <td colspan="3" class="sensor-value">${format(meterData.Freq, 2)} Hz</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">ENERGY Total</td>
                            <td colspan="2">kWh</td>
                            <td>kvarh</td>
                        </tr>
                        <tr>
                            <td colspan="2" class="sensor-value">${format(meterData.kWh_Total, 2)}</td>
                            <td class="sensor-value">${format(meterData.kvarh_Total, 2)}</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">kWh</td>
                            <td colspan="2">Forward</td>
                            <td>Backward</td>
                        </tr>
                        <tr>
                            <td colspan="2" class="sensor-value">${format(meterData.kWh_Forward, 2)}</td>
                            <td class="sensor-value">${format(meterData.kWh_Backward, 2)}</td>
                        </tr>
                        <tr>
                            <td rowspan="2" class="table-label">kvarh</td>
                            <td colspan="2">Forward</td>
                            <td>Backward</td>
                        </tr>
                        <tr>
                            <td colspan="2" class="sensor-value">${format(meterData.kvarh_Forward, 2)}</td>
                            <td class="sensor-value">${format(meterData.kvarh_Backward, 2)}</td>
                        </tr>
                        <tr style="border-top: 1px dashed var(--border-color); font-size: 11px;">
                            <td colspan="4" style="color: var(--text-secondary); padding: 8px 0 0 0; border: none; text-align: left; line-height: 1.6;">
                                <div><strong>Last Read:</strong> ${lastReadTime}</div>
                                <div><strong>Last MQTT Send:</strong> ${lastMqttTime} <span style="color: ${isMqttSuccess ? 'var(--success-color)' : (mqttInfo.status === 'Unknown' ? 'var(--text-secondary)' : 'var(--error-color)')}; font-weight: bold;">(${mqttStatusText})</span></div>
                            </td>
                        </tr>
                    </table>
                </div>
                `;
            });
            
            grid.innerHTML = html;
        }

        function fetchData() {
            fetch('/api/data')
                .then(response => response.json())
                .then(data => {
                    updateStatusCards(data);
                    populateForm(data.config);
                    generateMeterCards(data);
                })
                .catch(error => {
                    console.error('Error fetching data:', error);
                    document.getElementById('modbus-state').textContent = 'Server Offline';
                    document.getElementById('mqtt-state').textContent = 'Server Offline';
                });
        }

        document.getElementById('SLAVE_IDS').addEventListener('input', renderSlaveLabelInputs);

        document.getElementById('settings-form').addEventListener('submit', function(e) {
            e.preventDefault();
            const saveBtn = document.getElementById('save-btn');
            saveBtn.disabled = true;
            saveBtn.textContent = 'Saving...';

            const slaveLabelInputs = document.querySelectorAll('.slave-label-input');
            const slaveLabels = {};
            slaveLabelInputs.forEach(input => {
                const id = input.getAttribute('data-slave-id');
                slaveLabels[id] = input.value.trim();
            });

            const payload = {
                CONFIG_PASSWORD: configPIN,
                SERIAL_PORT: document.getElementById('SERIAL_PORT').value,
                BAUDRATE: parseInt(document.getElementById('BAUDRATE').value),
                SLAVE_IDS: document.getElementById('SLAVE_IDS').value,
                SLAVE_LABELS: slaveLabels,
                WEB_PORT: parseInt(document.getElementById('WEB_PORT').value),
                SEND_INTERVAL_SECONDS: parseInt(document.getElementById('SEND_INTERVAL_SECONDS').value) * 60,
                SAMPLE_COUNT: parseInt(document.getElementById('SAMPLE_COUNT').value),
                MQTT_BROKER: document.getElementById('MQTT_BROKER').value,
                MQTT_PORT: parseInt(document.getElementById('MQTT_PORT').value),
                MQTT_USERNAME: document.getElementById('MQTT_USERNAME').value,
                MQTT_PASSWORD: document.getElementById('MQTT_PASSWORD').value,
                MQTT_TOPIC_ENERGY: document.getElementById('MQTT_TOPIC_ENERGY').value,
                MQTT_TOPIC_KWH: document.getElementById('MQTT_TOPIC_KWH').value
            };

            fetch('/api/settings', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(payload)
            })
            .then(response => response.json())
            .then(data => {
                saveBtn.disabled = false;
                saveBtn.textContent = 'Save Settings';
                if (data.status === 'success') {
                    showToast(data.message, true);
                    lockConfiguration();
                    isConfigLoaded = false;
                    fetchData();
                } else {
                    showToast(data.message, false);
                }
            })
            .catch(error => {
                saveBtn.disabled = false;
                saveBtn.textContent = 'Save Settings';
                showToast('Terjadi kesalahan saat menyimpan data.', false);
                console.error('Error saving settings:', error);
            });
        });

        fetchData();
        setInterval(fetchData, 3000);
    </script>
</body>
</html>"""

@web_app.route('/')
def index():
    return render_template_string(INDEX_HTML)

@web_app.route('/api/data')
def get_data():
    with state_lock:
        data = {
            "modbus_connected": shared_state["modbus_connected"],
            "mqtt_connected": shared_state["mqtt_connected"],
            "slave_status": shared_state["slave_status"],
            "mqtt_status": shared_state.get("mqtt_status", {}),
            "latest_data": shared_state["latest_data"],
            "config": {
                "SERIAL_PORT": "********",
                "BAUDRATE": "********",
                "SLAVE_IDS": active_config["SLAVE_IDS"],
                "SLAVE_LABELS": active_config.get("SLAVE_LABELS", {}),
                "MQTT_BROKER": "********",
                "MQTT_PORT": "********",
                "MQTT_USERNAME": "********",
                "MQTT_PASSWORD_SET": bool(active_config.get("MQTT_PASSWORD")),
                "MQTT_TOPIC_ENERGY": "********",
                "MQTT_TOPIC_KWH": "********",
                "SEND_INTERVAL_SECONDS": 0,
                "SAMPLE_COUNT": 0,
                "LOGGER_ID": "********",
                "WEB_PORT": 0
            }
        }
    return jsonify(data)

@web_app.route('/api/config')
def get_config():
    pin = request.args.get("pin", "")
    if pin != "1234":
        return jsonify({"status": "error", "message": "PIN/Password Konfigurasi Salah!"}), 403
    
    with state_lock:
        config_data = {
            "SERIAL_PORT": active_config["SERIAL_PORT"],
            "BAUDRATE": active_config["BAUDRATE"],
            "SLAVE_IDS": active_config["SLAVE_IDS"],
            "SLAVE_LABELS": active_config.get("SLAVE_LABELS", {}),
            "MQTT_BROKER": active_config["MQTT_BROKER"],
            "MQTT_PORT": active_config["MQTT_PORT"],
            "MQTT_USERNAME": active_config["MQTT_USERNAME"],
            "MQTT_PASSWORD_SET": bool(active_config.get("MQTT_PASSWORD")),
            "MQTT_TOPIC_ENERGY": active_config["MQTT_TOPIC_ENERGY"],
            "MQTT_TOPIC_KWH": active_config["MQTT_TOPIC_KWH"],
            "SEND_INTERVAL_SECONDS": active_config["SEND_INTERVAL_SECONDS"],
            "SAMPLE_COUNT": active_config["SAMPLE_COUNT"],
            "LOGGER_ID": active_config["LOGGER_ID"],
            "WEB_PORT": active_config["WEB_PORT"]
        }
    return jsonify({"status": "success", "config": config_data})

@web_app.route('/api/settings', methods=['POST'])
def save_settings():
    try:
        req_data = request.json
        
        # Validasi PIN / Password Konfigurasi
        config_password = req_data.get("CONFIG_PASSWORD", "")
        if config_password != "1234":
            return jsonify({"status": "error", "message": "PIN/Password Konfigurasi Salah!"}), 403
            
        new_config = {
            "SERIAL_PORT": str(req_data.get("SERIAL_PORT", active_config["SERIAL_PORT"])),
            "BAUDRATE": int(req_data.get("BAUDRATE", active_config["BAUDRATE"])),
            "MQTT_BROKER": str(req_data.get("MQTT_BROKER", active_config["MQTT_BROKER"])),
            "MQTT_PORT": int(req_data.get("MQTT_PORT", active_config["MQTT_PORT"])),
            "MQTT_USERNAME": str(req_data.get("MQTT_USERNAME", "")).strip(),
            "MQTT_TOPIC_ENERGY": str(req_data.get("MQTT_TOPIC_ENERGY", active_config["MQTT_TOPIC_ENERGY"])),
            "MQTT_TOPIC_KWH": str(req_data.get("MQTT_TOPIC_KWH", active_config["MQTT_TOPIC_KWH"])),
            "SEND_INTERVAL_SECONDS": int(req_data.get("SEND_INTERVAL_SECONDS", active_config["SEND_INTERVAL_SECONDS"])),
            "SAMPLE_COUNT": int(req_data.get("SAMPLE_COUNT", active_config["SAMPLE_COUNT"])),
            "LOGGER_ID": str(req_data.get("LOGGER_ID", active_config["LOGGER_ID"])),
            "WEB_PORT": int(req_data.get("WEB_PORT", active_config["WEB_PORT"])),
            "SLAVE_LABELS": req_data.get("SLAVE_LABELS", {})
        }
        
        # Keep old password if it's set and submitted as '********'
        pwd_input = req_data.get("MQTT_PASSWORD", "")
        if pwd_input == "********":
            new_config["MQTT_PASSWORD"] = active_config.get("MQTT_PASSWORD", "")
        else:
            new_config["MQTT_PASSWORD"] = pwd_input.strip()
            
        slave_ids_input = req_data.get("SLAVE_IDS")
        if isinstance(slave_ids_input, list):
            new_config["SLAVE_IDS"] = [int(x) for x in slave_ids_input]
        elif isinstance(slave_ids_input, str):
            new_config["SLAVE_IDS"] = [int(x.strip()) for x in slave_ids_input.split(",") if x.strip().isdigit()]
        else:
            new_config["SLAVE_IDS"] = active_config["SLAVE_IDS"]
            
        if save_config(new_config):
            return jsonify({"status": "success", "message": "Settings saved and applied successfully!"})
        else:
            return jsonify({"status": "error", "message": "Failed to save settings to config.json"}), 500
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 400

def run_web_server():
    global active_config
    try:
        web_app.run(host="0.0.0.0", port=active_config["WEB_PORT"], debug=False, use_reloader=False)
    except Exception as e:
        print(f"Gagal menjalankan Flask web server: {e}")


# Definisi Struktur Kolom File CSV Lokal - Tunggal
CSV_COLUMNS = [
    "type", "timestamp", "slave_id", "uploaded",
    "ua", "ub", "uc", "uab", "ubc", "uca",
    "ia", "ib", "ic",
    "pa", "pb", "pc", "p_total",
    "qa", "qb", "qc", "q_total",
    "sa", "sb", "sc", "s_total",
    "pf1", "pf2", "pf3", "pf_avg",
    "freq",
    "kwh_total", "kvarh_total",
    "kwh_forward", "kvarh_forward",
    "kwh_backward", "kvarh_backward"
]

FILENAME_CSV = f"{LOGGER_ID}_ENERGY-DATA.csv"

# --- HELPER FUNCTIONS FOR CSV LOGGING & SYNC ---

def save_row_to_csv(filename, row, columns):
    """
    Menyimpan satu baris data ke file CSV tertentu.
    Menulis header secara otomatis jika file baru dibuat.
    """
    file_exists = os.path.exists(filename)
    try:
        with open(filename, mode='a', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=columns)
            if not file_exists:
                writer.writeheader()
            writer.writerow(row)
        return True
    except Exception as e:
        print(f"Error menulis CSV lokal ({filename}): {e}")
        return False

def sync_offline_data(mqtt_client):
    """
    Mensinkronisasikan data offline dari file CSV lokal ke MQTT Broker.
    Memeriksa baris yang bertanda uploaded = 0, mempublish-nya ke broker,
    lalu mengubah statusnya menjadi uploaded = 1 setelah sukses.
    """
    global is_mqtt_connected
    if not is_mqtt_connected:
        print("Sync: MQTT tidak terhubung. Menunda sinkronisasi.")
        return
        
    print("\n[SYNC] Memulai pengecekan data lokal yang belum terkirim...")
    
    if not os.path.exists(FILENAME_CSV):
        print("[SYNC] Selesai, file CSV lokal tidak ditemukan.")
        return
        
    rows = []
    updated = False
    
    try:
        with open(FILENAME_CSV, mode='r', newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            rows = list(reader)
    except Exception as e:
        print(f"Sync: Gagal membaca file {FILENAME_CSV}: {e}")
        return
        
    total_synced = 0
    for row in rows:
        if row.get("uploaded") == "0":
            slave_id = int(row["slave_id"])
            timestamp_str = row["timestamp"]
            payload_type = row.get("type")
            
            try:
                if payload_type == "energy":
                    energy_payload = {
                        "timestamp": timestamp_str,
                        "slave_id": slave_id,
                        "voltage": {
                            "ua": format_decimal_string(row.get("ua"), 1),
                            "ub": format_decimal_string(row.get("ub"), 1),
                            "uc": format_decimal_string(row.get("uc"), 1),
                            "uab": format_decimal_string(row.get("uab"), 1),
                            "ubc": format_decimal_string(row.get("ubc"), 1),
                            "uca": format_decimal_string(row.get("uca"), 1)
                        },
                        "current": {
                            "ia": format_decimal_string(row.get("ia"), 3),
                            "ib": format_decimal_string(row.get("ib"), 3),
                            "ic": format_decimal_string(row.get("ic"), 3)
                        },
                        "power": {
                            "active": {
                                "pa": format_decimal_string(row.get("pa"), 1),
                                "pb": format_decimal_string(row.get("pb"), 1),
                                "pc": format_decimal_string(row.get("pc"), 1),
                                "total": format_decimal_string(row.get("p_total"), 1)
                            },
                            "reactive": {
                                "qa": format_decimal_string(row.get("qa"), 1),
                                "qb": format_decimal_string(row.get("qb"), 1),
                                "qc": format_decimal_string(row.get("qc"), 1),
                                "total": format_decimal_string(row.get("q_total"), 1)
                            },
                            "apparent": {
                                "sa": format_decimal_string(row.get("sa"), 1),
                                "sb": format_decimal_string(row.get("sb"), 1),
                                "sc": format_decimal_string(row.get("sc"), 1),
                                "total": format_decimal_string(row.get("s_total"), 1)
                            }
                        },
                        "power_factor": {
                            "pf1": format_decimal_string(row.get("pf1"), 3),
                            "pf2": format_decimal_string(row.get("pf2"), 3),
                            "pf3": format_decimal_string(row.get("pf3"), 3),
                            "avg": format_decimal_string(row.get("pf_avg"), 3)
                        },
                        "frequency": format_decimal_string(row.get("freq"), 2)
                    }
                    
                    payload_energy_str = json.dumps(energy_payload)
                    topic_energy = MQTT_TOPIC_ENERGY.format(slave_id=slave_id)
                    
                    # Buat kwh payload harian/log dalam format Wh (kWh * 1000)
                    kwh_val = float(row["kwh_total"]) if row.get("kwh_total") else 0.0
                    wh_val = int(round(kwh_val * 1000))
                    kwh_payload = {
                        "timestamp": timestamp_str,
                        "slave_id": slave_id,
                        "kwh": wh_val
                    }
                    payload_kwh_str = json.dumps(kwh_payload)
                    topic_kwh = MQTT_TOPIC_KWH.format(slave_id=slave_id)
                    
                    info_energy = mqtt_client.publish(topic_energy, payload_energy_str, qos=1)
                    info_energy.wait_for_publish(timeout=5)
                    
                    info_kwh = mqtt_client.publish(topic_kwh, payload_kwh_str, qos=1)
                    info_kwh.wait_for_publish(timeout=5)
                    
                    if info_energy.is_published() and info_kwh.is_published():
                        row["uploaded"] = "1"
                        updated = True
                        total_synced += 1
                        print(f"Sync [Slave ID: {slave_id}]: Sukses mengunggah data energy & kwh ({timestamp_str})")
                    else:
                        print(f"Sync [Slave ID: {slave_id}]: Gagal mempublikasikan data (timeout/offline)")
                        break
                        
                elif payload_type == "kwh":
                    kwh_val = float(row["kwh_total"]) if row.get("kwh_total") else 0.0
                    wh_val = int(round(kwh_val * 1000))
                    kwh_payload = {
                        "timestamp": timestamp_str,
                        "slave_id": slave_id,
                        "kwh": wh_val
                    }
                    
                    payload_str = json.dumps(kwh_payload)
                    topic = MQTT_TOPIC_KWH.format(slave_id=slave_id)
                    
                    info = mqtt_client.publish(topic, payload_str, qos=1)
                    info.wait_for_publish(timeout=5)
                    
                    if info.is_published():
                        row["uploaded"] = "1"
                        updated = True
                        total_synced += 1
                        print(f"Sync [Slave ID: {slave_id}]: Sukses mengunggah data kwh harian lama ({timestamp_str})")
                    else:
                        print(f"Sync [Slave ID: {slave_id}]: Gagal mempublikasikan data (timeout/offline)")
                        break
                else:
                    continue
                    
            except Exception as e:
                print(f"Sync: Gagal memproses baris data dari {FILENAME_CSV} ke MQTT ({e})")
                continue
                
    # Tulis ulang status terupdate ke file CSV
    if updated:
        try:
            with open(FILENAME_CSV, mode='w', newline='', encoding='utf-8') as f:
                writer = csv.DictWriter(f, fieldnames=CSV_COLUMNS)
                writer.writeheader()
                writer.writerows(rows)
        except Exception as e:
            print(f"Sync: Gagal menulis ulang status upload ke {FILENAME_CSV}: {e}")
            
    if total_synced > 0:
        print(f"[SYNC] Selesai! Berhasil mengirim {total_synced} data tertunda ke broker.")
    else:
        print("[SYNC] Selesai, seluruh data lokal sudah sinkron.")

# --- MAIN LOOP FUNCTION ---

def main():
    global active_config, SLAVE_IDS
    
    # Inisialisasi thread web server Flask
    web_thread = threading.Thread(target=run_web_server, daemon=True)
    web_thread.start()
    print(f"Flask Web Server berjalan di http://0.0.0.0:{active_config['WEB_PORT']}")

    # Variabel client lokal
    client = None
    mqtt_client = None

    # Variabel pelacak konfigurasi yang saat ini aktif
    current_port = None
    current_baudrate = None
    current_broker = None
    current_port_mqtt = None
    current_username = None
    current_password = None

    def init_modbus():
        nonlocal client, current_port, current_baudrate
        if client:
            try:
                client.close()
            except Exception:
                pass
        
        current_port = active_config["SERIAL_PORT"]
        current_baudrate = active_config["BAUDRATE"]
        
        client = ModbusSerialClient(
            port=current_port,
            baudrate=current_baudrate,
            stopbits=2,
            bytesize=8,
            parity='N',
            timeout=1
        )
        
        connected = client.connect()
        with state_lock:
            shared_state["modbus_connected"] = connected
        if connected:
            print(f"Modbus: Berhasil terhubung ke serial port {current_port}")
        else:
            print(f"Modbus: Gagal terhubung ke serial port {current_port}. Silakan cek fisik kabel / port di web UI.")

    def init_mqtt():
        nonlocal mqtt_client, current_broker, current_port_mqtt, current_username, current_password
        if mqtt_client:
            try:
                mqtt_client.loop_stop()
                mqtt_client.disconnect()
            except Exception:
                pass

        current_broker = active_config["MQTT_BROKER"]
        current_port_mqtt = active_config["MQTT_PORT"]
        current_username = active_config.get("MQTT_USERNAME", "")
        current_password = active_config.get("MQTT_PASSWORD", "")

        mqtt_client = mqtt.Client()
        if current_username:
            mqtt_client.username_pw_set(current_username, current_password)

        def on_connect(c, userdata, flags, rc):
            global is_mqtt_connected
            if rc == 0:
                print(f"MQTT: Berhasil terhubung ke Broker ({current_broker})")
                is_mqtt_connected = True
                with state_lock:
                    shared_state["mqtt_connected"] = True
                try:
                    sync_offline_data(c)
                except Exception as e:
                    print(f"MQTT: Gagal sinkronisasi otomatis: {e}")
            else:
                print(f"MQTT: Gagal terhubung ke Broker, Kode Respon: {rc}")
                is_mqtt_connected = False
                with state_lock:
                    shared_state["mqtt_connected"] = False

        def on_disconnect(c, userdata, rc):
            global is_mqtt_connected
            print(f"MQTT: Terputus dari Broker (rc={rc})")
            is_mqtt_connected = False
            with state_lock:
                shared_state["mqtt_connected"] = False

        mqtt_client.on_connect = on_connect
        mqtt_client.on_disconnect = on_disconnect

        try:
            mqtt_client.connect_async(current_broker, current_port_mqtt, keepalive=60)
            mqtt_client.loop_start()
            print(f"MQTT: Background loop dimulai untuk terhubung ke {current_broker}:{current_port_mqtt}...")
        except Exception as e:
            print(f"MQTT: Gagal inisialisasi broker MQTT: {e}")

    # Inisialisasi awal koneksi
    init_modbus()
    init_mqtt()

    # Menyimpan data sampling sementara untuk pengiriman real-time (interval pendek)
    send_accumulators = {}
    # Menyimpan data sampling sementara untuk pencatatan CSV per jam
    hourly_accumulators = {}
    # Menghitung jumlah pengiriman MQTT yang sukses per jam untuk masing-masing slave
    successful_mqtt_sends_this_hour = {}

    def update_accumulators():
        nonlocal send_accumulators, hourly_accumulators, successful_mqtt_sends_this_hour
        for slave_id in list(SLAVE_IDS):
            if slave_id not in send_accumulators:
                send_accumulators[slave_id] = []
            if slave_id not in hourly_accumulators:
                hourly_accumulators[slave_id] = []
            if slave_id not in successful_mqtt_sends_this_hour:
                successful_mqtt_sends_this_hour[slave_id] = 0
                
        # Hapus data slave yang tidak terdaftar
        for slave_id in list(send_accumulators.keys()):
            if slave_id not in SLAVE_IDS:
                send_accumulators.pop(slave_id, None)
                hourly_accumulators.pop(slave_id, None)
                successful_mqtt_sends_this_hour.pop(slave_id, None)

    update_accumulators()

    try:
        while True:
            # Cek jika ada sinyal pembaruan konfigurasi
            if config_changed_event.is_set():
                print("Config change detected! Memproses reload parameter logger...")
                
                # Re-init Modbus jika port / baudrate berubah
                if active_config["SERIAL_PORT"] != current_port or active_config["BAUDRATE"] != current_baudrate:
                    init_modbus()
                    
                # Re-init MQTT jika broker / port / user / password berubah
                if (active_config["MQTT_BROKER"] != current_broker or
                    active_config["MQTT_PORT"] != current_port_mqtt or
                    active_config.get("MQTT_USERNAME", "") != current_username or
                    active_config.get("MQTT_PASSWORD", "") != current_password):
                    init_mqtt()
                    
                update_accumulators()
                config_changed_event.clear()

            # Jeda sampling otomatis dihitung secara dinamis dari active_config
            current_interval = active_config["SEND_INTERVAL_SECONDS"]
            current_sample_count = active_config["SAMPLE_COUNT"]
            current_sample_delay = current_interval / max(1, current_sample_count)

            # Hitung waktu tunggu agar sampling berikutnya selaras dengan detik/menit bulat
            now = time.time()
            sleep_time = current_sample_delay - (now % current_sample_delay)
            if sleep_time < 0.1:
                sleep_time += current_sample_delay
                
            # Gunakan Event untuk sleep agar bisa diinterupsi seketika saat config disimpan
            if config_changed_event.wait(sleep_time):
                # Jika event di-set saat menunggu, langsung skip ke awal loop untuk reload config
                continue
            
            # Catat waktu saat sampling berhasil dimulai
            sample_time = time.time()
            current_local_time = datetime.now()
            current_date_str = current_local_time.strftime("%Y-%m-%d")
            
            for slave_id in list(SLAVE_IDS):
                # Inisialisasi data dengan nilai 0.0 untuk semua register
                data = {name: 0.0 for name, _, _, _, _ in REGISTERS_MAP}
                read_success = True
                
                # Membaca holding registers secara massal untuk slave_id saat ini
                try:
                    if not client or not shared_state["modbus_connected"]:
                        if client and client.connect():
                            with state_lock:
                                shared_state["modbus_connected"] = True
                        else:
                            with state_lock:
                                shared_state["modbus_connected"] = False
                            read_success = False

                    if read_success:
                        result = safe_read_holding_registers(client, START_ADDRESS, COUNT, slave_id)
                        
                        if result is None or result.isError():
                            log_print(f"Slave ID: {slave_id} Error membaca register: {result}")
                            read_success = False
                except Exception as e:
                    log_print(f"Slave ID: {slave_id} Gagal komunikasi/koneksi: {e}")
                    read_success = False
                
                # Jika pembacaan berhasil, lakukan dekoding nilai asli
                if read_success:
                    # Meter menggunakan Word Order 'big' (Big Endian)
                    word_order = 'big'
                    
                    for name, addr, multiplier, unit, desc in REGISTERS_MAP:
                        try:
                            raw_val = decode_register_32(result.registers, START_ADDRESS, addr, word_order)
                            data[name] = raw_val * multiplier
                        except Exception as e:
                            data[name] = 0.0
                
                # Update status pembacaan ke shared_state untuk Web UI
                with state_lock:
                    timestamp_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    shared_state["slave_status"][slave_id] = {
                        "status": "Success" if read_success else "Failed",
                        "timestamp": timestamp_str
                    }
                    if read_success:
                        shared_state["latest_data"][slave_id] = data.copy()
                
                # Simpan data sampling ke akumulator jika slave_id masih valid
                if slave_id in send_accumulators:
                    send_accumulators[slave_id].append(data)
                if slave_id in hourly_accumulators:
                    hourly_accumulators[slave_id].append(data)
                
                # Tampilkan log pembacaan satu baris ringkas
                timestamp_log = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                status_str = "OK" if read_success else "FAIL"
                print(f"[{timestamp_log}] [SLAVE {slave_id}] [{len(send_accumulators.get(slave_id, []))}/{current_sample_count}] [{status_str}]")
                
                # --- LOGIKA PENGIRIMAN DATA REAL-TIME ---
                current_send_idx = int(sample_time - 1) // current_interval
                next_send_time = sample_time + current_sample_delay
                next_send_idx = int(next_send_time - 1) // current_interval
                is_send_interval_end = current_send_idx != next_send_idx
                
                accum_len = len(send_accumulators.get(slave_id, []))
                if accum_len >= current_sample_count or is_send_interval_end:
                    samples_send = send_accumulators.get(slave_id, [])
                    if samples_send:
                        # Saring data sampling yang sukses (tidak bernilai 0 semua) untuk dirata-rata
                        success_samples_send = [s for s in samples_send if any(v > 0 for k, v in s.items() if k not in ["slave_id", "timestamp"])]
                        if not success_samples_send:
                            success_samples_send = samples_send
                            
                        # Kategori parameter yang akan dirata-rata
                        instant_keys = [
                            "UA", "UB", "UC", "UAB", "UBC", "UCA",
                            "IA", "IB", "IC",
                            "PA", "PB", "PC", "P_Total",
                            "QA", "QB", "QC", "Q_Total",
                            "SA", "SB", "SC", "S_Total",
                            "PF1", "PF2", "PF3", "PF_Avg",
                            "Freq"
                        ]
                        
                        latest_sample_send = samples_send[-1]
                        cumulative_keys = [
                            "kWh_Total", "kvarh_Total",
                            "kWh_Forward", "kWh_Backward",
                            "kvarh_Forward", "kvarh_Backward"
                        ]
                        
                        final_data_send = {}
                        for key in instant_keys:
                            final_data_send[key] = sum(s[key] for s in success_samples_send) / len(success_samples_send)
                        for key in cumulative_keys:
                            final_data_send[key] = latest_sample_send[key]
                            
                        timestamp_str_send = get_aligned_timestamp(current_local_time, current_interval)
                        
                        try:
                            # Buat payload energy (tanpa objek energy)
                            energy_payload = {
                                "timestamp": timestamp_str_send,
                                "slave_id": slave_id,
                                "voltage": {
                                    "ua": format_decimal_string(final_data_send["UA"], 1),
                                    "ub": format_decimal_string(final_data_send["UB"], 1),
                                    "uc": format_decimal_string(final_data_send["UC"], 1),
                                    "uab": format_decimal_string(final_data_send["UAB"], 1),
                                    "ubc": format_decimal_string(final_data_send["UBC"], 1),
                                    "uca": format_decimal_string(final_data_send["UCA"], 1)
                                },
                                "current": {
                                    "ia": format_decimal_string(final_data_send["IA"], 3),
                                    "ib": format_decimal_string(final_data_send["IB"], 3),
                                    "ic": format_decimal_string(final_data_send["IC"], 3)
                                },
                                "power": {
                                    "active": {
                                        "pa": format_decimal_string(final_data_send["PA"], 1),
                                        "pb": format_decimal_string(final_data_send["PB"], 1),
                                        "pc": format_decimal_string(final_data_send["PC"], 1),
                                        "total": format_decimal_string(final_data_send["P_Total"], 1)
                                    },
                                    "reactive": {
                                        "qa": format_decimal_string(final_data_send["QA"], 1),
                                        "qb": format_decimal_string(final_data_send["QB"], 1),
                                        "qc": format_decimal_string(final_data_send["QC"], 1),
                                        "total": format_decimal_string(final_data_send["Q_Total"], 1)
                                    },
                                    "apparent": {
                                        "sa": format_decimal_string(final_data_send["SA"], 1),
                                        "sb": format_decimal_string(final_data_send["SB"], 1),
                                        "sc": format_decimal_string(final_data_send["SC"], 1),
                                        "total": format_decimal_string(final_data_send["S_Total"], 1)
                                    }
                                },
                                "power_factor": {
                                    "pf1": format_decimal_string(final_data_send["PF1"], 3),
                                    "pf2": format_decimal_string(final_data_send["PF2"], 3),
                                    "pf3": format_decimal_string(final_data_send["PF3"], 3),
                                    "avg": format_decimal_string(final_data_send["PF_Avg"], 3)
                                },
                                "frequency": format_decimal_string(final_data_send["Freq"], 2)
                            }
                            
                            payload_energy = json.dumps(energy_payload)
                            topic_energy = MQTT_TOPIC_ENERGY.format(slave_id=slave_id)
                            
                            # Buat payload kwh dalam format Wh (kWh * 1000)
                            kwh_val = final_data_send["kWh_Total"]
                            wh_val = int(round(kwh_val * 1000))
                            kwh_payload = {
                                "timestamp": timestamp_str_send,
                                "slave_id": slave_id,
                                "kwh": wh_val
                            }
                            payload_kwh = json.dumps(kwh_payload)
                            topic_kwh = MQTT_TOPIC_KWH.format(slave_id=slave_id)
                            
                            print(f"[{timestamp_str_send}] MQTT Energy Payload [Slave {slave_id}]: {payload_energy}")
                            print(f"[{timestamp_str_send}] MQTT KWH Payload [Slave {slave_id}]: {payload_kwh}")
                            
                            mqtt_send_ok = False
                            if is_mqtt_connected:
                                info_energy = mqtt_client.publish(topic_energy, payload_energy, qos=1)
                                info_energy.wait_for_publish(timeout=2)
                                
                                info_kwh = mqtt_client.publish(topic_kwh, payload_kwh, qos=1)
                                info_kwh.wait_for_publish(timeout=2)
                                
                                if info_energy.is_published() and info_kwh.is_published():
                                    mqtt_send_ok = True
                                    if slave_id in successful_mqtt_sends_this_hour:
                                        successful_mqtt_sends_this_hour[slave_id] += 1
                                    print(f"[{timestamp_str_send}] MQTT [Slave ID: {slave_id}]: PUBLISH SUCCESS (energy & kwh)")
                                else:
                                    print(f"[{timestamp_str_send}] MQTT [Slave ID: {slave_id}]: PUBLISH FAILED")
                            else:
                                print(f"[{timestamp_str_send}] MQTT [Slave ID: {slave_id}]: PUBLISH FAILED (MQTT offline)")
                            
                            with state_lock:
                                shared_state["mqtt_status"][slave_id] = {
                                    "status": "Success" if mqtt_send_ok else "Failed",
                                    "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                                }
                        except Exception as e:
                            log_print(f"MQTT [Slave ID: {slave_id}]: Gagal memproses/mengirim data real-time ({e})")
                            with state_lock:
                                shared_state["mqtt_status"][slave_id] = {
                                    "status": "Failed",
                                    "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                                }
                            
                        if slave_id in send_accumulators:
                            send_accumulators[slave_id] = []
                    
                # --- LOGIKA PENULISAN DATA RATA-RATA PER JAM KE CSV LOKAL ---
                HOURLY_INTERVAL_SECONDS = 3600
                current_hour_idx = int(sample_time - 1) // HOURLY_INTERVAL_SECONDS
                next_hour_time = sample_time + current_sample_delay
                next_hour_idx = int(next_hour_time - 1) // HOURLY_INTERVAL_SECONDS
                is_hour_interval_end = current_hour_idx != next_hour_idx
                
                if is_hour_interval_end:
                    samples_hour = hourly_accumulators.get(slave_id, [])
                    if samples_hour:
                        success_samples_hour = [s for s in samples_hour if any(v > 0 for k, v in s.items() if k not in ["slave_id", "timestamp"])]
                        if not success_samples_hour:
                            success_samples_hour = samples_hour
                            
                        final_data_hour = {}
                        for key in instant_keys:
                            final_data_hour[key] = sum(s[key] for s in success_samples_hour) / len(success_samples_hour)
                        for key in cumulative_keys:
                            final_data_hour[key] = samples_hour[-1][key]
                            
                        sends_count = successful_mqtt_sends_this_hour.get(slave_id, 0)
                        uploaded_status = 1 if sends_count > 0 else 0
                        
                        timestamp_str_hour = current_local_time.strftime("%Y-%m-%d %H:%M:00")
                        
                        energy_row = {
                            "type": "energy",
                            "timestamp": timestamp_str_hour,
                            "slave_id": slave_id,
                            "uploaded": uploaded_status,
                            "ua": round(final_data_hour["UA"], 1),
                            "ub": round(final_data_hour["UB"], 1),
                            "uc": round(final_data_hour["UC"], 1),
                            "uab": round(final_data_hour["UAB"], 1),
                            "ubc": round(final_data_hour["UBC"], 1),
                            "uca": round(final_data_hour["UCA"], 1),
                            "ia": round(final_data_hour["IA"], 3),
                            "ib": round(final_data_hour["IB"], 3),
                            "ic": round(final_data_hour["IC"], 3),
                            "pa": round(final_data_hour["PA"], 1),
                            "pb": round(final_data_hour["PB"], 1),
                            "pc": round(final_data_hour["PC"], 1),
                            "p_total": round(final_data_hour["P_Total"], 1),
                            "qa": round(final_data_hour["QA"], 1),
                            "qb": round(final_data_hour["QB"], 1),
                            "qc": round(final_data_hour["QC"], 1),
                            "q_total": round(final_data_hour["Q_Total"], 1),
                            "sa": round(final_data_hour["SA"], 1),
                            "sb": round(final_data_hour["SB"], 1),
                            "sc": round(final_data_hour["SC"], 1),
                            "s_total": round(final_data_hour["S_Total"], 1),
                            "pf1": round(final_data_hour["PF1"], 3),
                            "pf2": round(final_data_hour["PF2"], 3),
                            "pf3": round(final_data_hour["PF3"], 3),
                            "pf_avg": round(final_data_hour["PF_Avg"], 3),
                            "freq": round(final_data_hour["Freq"], 2),
                            "kwh_total": round(final_data_hour["kWh_Total"], 2),
                            "kvarh_total": round(final_data_hour["kvarh_Total"], 2),
                            "kwh_forward": round(final_data_hour["kWh_Forward"], 2),
                            "kvarh_forward": round(final_data_hour["kvarh_Forward"], 2),
                            "kwh_backward": round(final_data_hour["kWh_Backward"], 2),
                            "kvarh_backward": round(final_data_hour["kvarh_Backward"], 2)
                        }
                        
                        if save_row_to_csv(FILENAME_CSV, energy_row, CSV_COLUMNS):
                            status_msg = "Arsip Lokal (Tidak dikirim ke MQTT karena online)" if uploaded_status == 1 else "Buffer Offline (Akan dikirim ke MQTT saat online)"
                            print(f"[LOCAL LOG - ENERGY] Rata-rata per jam disimpan untuk Slave ID: {slave_id}. Status: {status_msg}")
                            
                        if slave_id in hourly_accumulators:
                            hourly_accumulators[slave_id] = []
                            
                        if slave_id in successful_mqtt_sends_this_hour:
                            successful_mqtt_sends_this_hour[slave_id] = 0
                            
                        try:
                            sync_offline_data(mqtt_client)
                        except Exception as e:
                            print(f"Sync Error: {e}")

    except KeyboardInterrupt:
        print("\nProgram dihentikan.")
    finally:
        # Hentikan background loop MQTT dan putus koneksi dengan aman
        try:
            if mqtt_client:
                mqtt_client.loop_stop()
                mqtt_client.disconnect()
                print("MQTT: Koneksi diputuskan dengan aman.")
        except Exception:
            pass
        try:
            if client:
                client.close()
        except Exception:
            pass

if __name__ == "__main__":
    main()