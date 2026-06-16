# Storage Prediction Comparison — SEMS AIoT

Baseline chip: ESP32-WROOM-32 4MB (dev) / ESP32-WROOM-32UE-N16 16MB (prod)  
Baseline firmware (after revert, measured): **899 KB = 68.6%** dari 1280 KB app slot

---

## Partition Layout Reference

```
4MB Flash
├── nvs        20 KB   config, TOU history, credentials
├── otadata     8 KB   OTA state (active slot pointer)
├── app0     1280 KB   firmware aktif
├── app1     1280 KB   OTA download slot
└── spiffs   1476 KB   reserved
```

---

## Library Delta

| Komponen | WLAN build | LAN build | Delta |
|----------|-----------|-----------|-------|
| WiFi stack (framework blob) | Selalu ada | Selalu ada | 0 |
| `WiFiClient` / `WiFiClientSecure` | ✅ aktif | ✅ (untuk config AP) | ~0 |
| `arduino-libraries/Ethernet` (W5500) | ❌ tidak dikompile | ✅ dikompile | **+45–55 KB** |
| `EthernetClient` untuk MQTT | ❌ | ✅ | dalam delta atas |

> WiFi AP untuk config mode tetap ada di kedua build — ESP32 selalu butuh WiFi untuk AP.

---

## Skenario 1 — Basic WLAN Setup

```
app0  [████████████░░░░░░░░]  899 KB / 1280 KB  (70.2%)
app1  [░░░░░░░░░░░░░░░░░░░░]  kosong
NVS   [░]  ~1.5 KB dari 20 KB
```

| Item | Nilai |
|------|-------|
| Firmware size | **~899 KB** |
| Flash usage | **68.6%** |
| Headroom | 381 KB |
| OTA available | ✅ app1 kosong, siap terima download |
| Library | PubSubClient + U8g2 + WiFi |
| MQTT transport | `WiFiClient` |
| Config AP | `SEMS-SETUP-{mac}` via WiFi |

---

## Skenario 2 — Basic LAN Setup

```
app0  [█████████████░░░░░░░]  950 KB / 1280 KB  (74.2%)
app1  [░░░░░░░░░░░░░░░░░░░░]  kosong
NVS   [░]  ~1.5 KB dari 20 KB
```

| Item | Nilai |
|------|-------|
| Firmware size | **~950 KB** (baseline + Ethernet lib ~50 KB) |
| Flash usage | **~74%** |
| Headroom | 330 KB |
| OTA available | ✅ app1 kosong, siap terima download |
| Library | PubSubClient + U8g2 + Ethernet |
| MQTT transport | `EthernetClient` via W5500 |
| Config AP | WiFi AP tetap aktif untuk web UI config |

---

## Skenario 3 — Setelah Switch WLAN → LAN (via OTA)

### Fase 1: Saat OTA download berlangsung

```
app0  [████████████░░░░░░░░]  899 KB   ← AKTIF, jalan normal
app1  [█████░░░░░░░░░░░░░░░]  download LAN firmware in progress...
```

### Fase 2: Download selesai, sebelum reboot

```
app0  [████████████░░░░░░░░]  899 KB   ← masih aktif
app1  [█████████████░░░░░░░]  950 KB   ← LAN firmware siap
otadata: pointer masih ke app0
```

### Fase 3: Setelah reboot

```
app0  [█████████████░░░░░░░]  950 KB   ← AKTIF (LAN firmware)
app1  [████████████░░░░░░░░]  899 KB   ← backup WLAN (rollback tersedia)
otadata: pointer ke app0
NVS: config TETAP (NVS tidak di-wipe saat OTA)
```

| Item | Nilai |
|------|-------|
| Active firmware | LAN build, 950 KB |
| Flash usage total | 899 + 950 = **1849 KB dari 2560 KB** (72%) |
| Rollback | ✅ app1 masih simpan WLAN firmware |
| NVS config | ✅ preserved — SSID, MQTT, Modbus, TOU history semua aman |
| Config AP | masih jalan via WiFi |

---

## Skenario 4 — Setelah Switch LAN → WLAN (via OTA)

### Fase 1: Saat OTA download berlangsung

```
app0  [█████████████░░░░░░░]  950 KB   ← AKTIF (LAN), jalan normal via Ethernet
app1  [████░░░░░░░░░░░░░░░░]  download WLAN firmware in progress...
```

### Fase 2: Setelah reboot

```
app0  [████████████░░░░░░░░]  899 KB   ← AKTIF (WLAN firmware)
app1  [█████████████░░░░░░░]  950 KB   ← backup LAN (rollback tersedia)
otadata: pointer ke app0
NVS: config TETAP
```

| Item | Nilai |
|------|-------|
| Active firmware | WLAN build, 899 KB |
| Flash usage total | 899 + 950 = **1849 KB dari 2560 KB** (72%) |
| Rollback | ✅ app1 masih simpan LAN firmware |
| NVS config | ✅ preserved |

---

## Ringkasan Perbandingan

| Skenario | Active firmware | Flash total terpakai | % dari 2560 KB | Rollback |
|----------|----------------|---------------------|----------------|---------|
| 1. Basic WLAN | 899 KB | 899 KB | **35%** | ❌ app1 kosong |
| 2. Basic LAN | ~950 KB | ~950 KB | **37%** | ❌ app1 kosong |
| 3. After WLAN→LAN | 950 KB | 1849 KB | **72%** | ✅ WLAN di app1 |
| 4. After LAN→WLAN | 899 KB | 1849 KB | **72%** | ✅ LAN di app1 |

> Skenario 3 dan 4 identik dari sisi total flash usage — yang berubah hanya mana yang aktif.

---

## Catatan untuk 16MB Production (JLCPCB)

Dengan 16MB flash, app slot bisa diperbesar ke 3–4 MB masing-masing.  
Semua skenario di atas menggunakan < 5% dari total 16 MB.  
OTA tidak pernah jadi constraint di production board.

---

## Yang Perlu Dibangun

Untuk OTA switch bisa jalan, firmware butuh:

1. **OTA download function** — `HTTPClient` fetch binary dari URL (GitHub Releases atau server)
2. **`/api/ota/switch`** route di web UI — trigger download + flash
3. **Version endpoint** — cek versi sebelum download supaya tidak re-flash yang sama
4. **Partition table custom** — `partitions_ota.csv` agar app slot = 1280 KB (sudah default, tidak perlu ubah untuk 4MB)
