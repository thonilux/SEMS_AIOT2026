# Ethernet Web Server Limitation — SEMS AIoT

**Tanggal ditemukan:** 17 Juni 2026  
**Status:** Confirmed, workaround documented

---

## Masalah

`WebServer` library Arduino ESP32 **tidak bisa listen di W5500 Ethernet interface**.

Percobaan: start `WebServer` di port 80, ESP32 dapat IP dari W5500 DHCP (`192.168.1.15`), tapi semua koneksi ke port 80 di-reject (`ERR_CONNECTION_REFUSED`).

Ping ke IP berhasil (ICMP jalan), tapi TCP port 80 tidak respond.

## Root Cause

ESP32 Arduino `WebServer` menggunakan lwIP TCP stack yang terintegrasi dengan WiFi driver. W5500 menggunakan TCP stack internal chip sendiri yang berkomunikasi via SPI — **dua stack terpisah, tidak terhubung**.

```
ESP32 internal:
  WiFi driver → lwIP → WebServer (port 80) ✅

W5500 external:
  LAN cable → W5500 chip → SPI → EthernetClient/EthernetServer ❌ WebServer tidak bisa bind di sini
```

## Yang Bisa Jalan via W5500

| Use case | Bisa? | Keterangan |
|----------|-------|------------|
| MQTT client (PubSubClient) | ✅ | EthernetClient sebagai transport |
| HTTP client (HTTPClient) | ✅ | EthernetClient sebagai transport |
| Ping (ICMP) | ✅ | W5500 handle sendiri |
| WebServer (incoming HTTP) | ❌ | Butuh lwIP, tidak ada di W5500 stack |
| OTA download | ✅ | Bisa pakai HTTPClient + EthernetClient |

## Workaround untuk Web UI Access

Akses web UI **hanya via WiFi**:

1. **Normal mode + WiFi STA connected** → buka `http://<wifi-ip>/`
2. **Config AP mode** → connect ke `SEMS-SETUP-xxxxxx`, buka `http://192.168.4.1/`
3. **Fallback AP** → otomatis aktif setelah WiFi STA timeout 15 detik

W5500 Ethernet tetap digunakan untuk **MQTT transport** (primary) dengan fallback ke WiFi.

## Alternatif Jika Web UI via LAN Diperlukan

Opsi yang memungkinkan (belum diimplementasi):

1. **HTTP server manual di atas `EthernetServer`** — parse HTTP request dan kirim response secara manual tanpa library. Tidak ada routing helper, HTML harus minimal. Feasible tapi effort cukup besar.

2. **ESP-IDF raw socket** — bypass Arduino layer, buat TCP server langsung di lwIP dengan bind ke interface W5500. Kompleks, keluar dari Arduino framework.

3. **Tidak perlu** — untuk produk B2B/industrial, akses via WiFi AP sudah cukup. Web UI hanya dipakai saat commissioning.

## Keputusan

Untuk deadline B4T Juli 2026: **tidak implementasi web UI via Ethernet**. W5500 fokus ke MQTT transport saja. Web UI akses via WiFi AP.
