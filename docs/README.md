# 📚 SEMS AIoT 2026 Documentation Hub

Pusat dokumentasi teknis, arsitektur, desain perangkat keras, integrasi API, panduan operasional, hingga materi luaran project **SEMS AIoT 2026 (PM1611 RS485 Reader & Controller)**.

---

## 🗂️ Struktur Direktori Dokumentasi

```text
docs/
├── architecture/       Arsitektur sistem, firmware engine, roadmap, dan storage strategy
├── hardware/           Skematik, layout PCB, render 3D, pinout ESP32, LCD, dan analisa komponen
├── api/                Spesifikasi Modbus config API, config mode API, dan kontrak payload
├── operations/         Laporan kesiapan tools, status progress project, dan prosedur operasional
├── journal/            Catatan riset harian, pengujian bertahap, dan changelog pengembangan
├── modbus-profiles/    Profil register Modbus (Schneider EM6400, PM2xxx, Lab Kontrol, dsb.)
├── oled/               Mockup UI/UX layar OLED 0.96", layout dashboard, status & toast design
├── webui/              Mockup & screenshot antarmuka Web UI (Normal Mode vs Config Mode)
├── nodered/            Flow Node-RED integrasi & dashboard monitoring kesehatan perangkat
└── [Configs & Guides]  File konfigurasi ESPHome (YAML), manual book, dan naskah luaran video
```

---

## 📑 Navigasi Dokumen Utama

### 1. 🔧 Hardware & Desain Fisik ([docs/hardware/](hardware/README.md))
* **Desain & Manufaktur:**
  * [Schematic Diagram (PDF)](hardware/schematic.pdf) — Skematik kelistrikan lengkap
  * [PCB Layout & Routing (PDF)](hardware/pcb.pdf) — Layout PCB & jalur routing siap cetak
  * [3D Render / Enclosure Preview](hardware/3D_Lab_1_2026-09-29.png) — Visualisasi modul 3D & case
* **Spesifikasi & Pinout:**
  * [Prototype Pin Map](hardware/pin-map.md) — Alokasi pin GPIO ESP32, RS485 MAX485, LCD/OLED, Relay
  * [ESP32 Hardware Capability Analysis](hardware/esp32.md) — Analisis chip, strapping pin, memori, & perifer
  * [LCD Hardware Analysis](hardware/lcd.md) — Evaluasi display 16x2 I2C vs OLED I2C/SPI
  * [Previous Prototype Schematic Notes](hardware/prototype-schematic.md) — Catatan evaluasi prototipe lama

### 2. 🏗️ Arsitektur & Firmware ([docs/architecture/](architecture/README.md))
* [One-Man Army Development Roadmap](architecture/one-man-roadmap.md) — Tahapan target fitur & timeline
* [Configurable Modbus Engine](architecture/configurable-modbus-engine.md) — Arsitektur dynamic polling Modbus RTU
* [Config Mode Architecture](architecture/config-mode.md) & [Config Model](architecture/config-model.md) — Arsitektur dual-mode (Normal vs AP/Config)
* [Flash & Storage Strategy](architecture/flash-and-storage-strategy.md) & [Storage Prediction Comparison](architecture/storage-prediction-comparison.md) — Strategi NVS / LittleFS / Flash wear leveling
* [Ethernet Webserver Limitations](architecture/ethernet-webserver-limitation.md) — Analisis performa webserver ESP32 + Ethernet/WiFi

### 3. 📨 API & Protokol Komunikasi ([docs/api/](api/README.md))
* [Modbus Configuration API](api/modbus-config-api.md) — Format REST API untuk update register & baudrate
* [Config Mode API Specification](api/config-mode-api.md) — Endpoint setting WiFi, MQTT, Network, dan System

### 4. 🧭 Operasional & Prosedur ([docs/operations/](operations/README.md))
* [Manual Book / Panduan Pengoperasian](manual_book.md) — Buku panduan lengkap instalasi & penggunaan alat
* [Project Progress Report](operations/project-progress-report.md) — Laporan pencapaian milestone & status fitur
* [Laptop Tooling Readiness Report](operations/laptop-tooling-report.md) — Setup environment VS Code, ESP-IDF, PlatformIO

### 5. 📊 Profil Modbus & Integrasi
* [Schneider EM6400 / PM2xxx Register Map](modbus-profiles/schneider-em6400-pm2xxx.md)
* [Lab Kontrol Register Map](lab_kontrol_register_map.md) & [Lab Kontrol YAML Config](lab_kontrol.yaml)
* [Node-RED Health Flow](nodered/sems-health-flow.json) — Template flow Node-RED untuk MQTT telemetry & health status

### 6. 🎨 Antarmuka Visual (OLED & WebUI)
* **OLED Display ([docs/oled/](oled/)):**
  * Dashboard status, WiFi connected/disconnected, Ethernet LAN, MQTT heartbeat, and toast notifications.
* **Web Configuration UI ([docs/webui/](webui/)):**
  * Mockup antarmuka root dashboard, Network, MQTT, Modbus configuration, Relay, System, dan OTA Firmware update.

### 7. 🎬 Naskah & Luaran Video
* [Rancangan Alur Video](rancangan-alur-video.md) — Storyboard, naskah narasi, scene-by-scene video demonstrasi
* [Ketentuan Luaran Video Dortek 2026 SEMS](Ketentuan%20Luaran%20Video.md) — Panduan format dan ketentuan teknis video luaran

---

> 💡 **Catatan**: Seluruh source code firmware berada di folder `firmware/` dan `firmware-lab/`. Direktori `docs/` dikhususkan untuk referensi teknis, dokumentasi pendukung, dan aset artefak proyek.
