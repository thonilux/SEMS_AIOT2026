# Flash Usage & Storage Strategy — SEMS AIoT

## 1. Hardware Baseline

| Variant | Chip | Flash | Konteks |
|---------|------|-------|---------|
| Development (Tokopedia) | ESP32-WROOM-32 | 4 MB | Testing harian |
| Production (JLCPCB) | ESP32-WROOM-32UE-N16 | 16 MB | B4T, Komdigi, produk |

**4MB dijadikan baseline constraint.** Firmware yang aman di 4MB otomatis aman di 16MB.

---

## 2. Partition Layout (4MB, OTA enabled)

```
Offset     Name       Size      Keterangan
─────────────────────────────────────────────
0x9000     nvs        20 KB     Config, history, credentials
0xe000     otadata     8 KB     OTA state
0x10000    app0     1280 KB     Firmware aktif
0x150000   app1     1280 KB     OTA slot
0x290000   spiffs   1476 KB     Reserved (tidak digunakan aktif)
```

Total = 4096 KB.

---

## 3. Flash Usage Tracking

| State | Flash used | % dari app slot (1280 KB) |
|-------|-----------|---------------------------|
| Setelah revert ke baseline (082bdf6) | 899 KB | **68.6%** |
| Estimasi setelah W5500 + rename | ~960 KB | **~75%** |
| Batas aman yang ditetapkan | < 1024 KB | **< 80%** |
| Hard limit (OTA masih bisa jalan) | < 1280 KB | 100% |

### Aturan main

- **< 80%**: zona aman, OTA bisa jalan, ada headroom development
- **80–90%**: zona kuning, OTA masih bisa tapi mepet
- **> 90%**: zona merah, OTA berisiko gagal

### Yang boleh ditambah tanpa khawatir

- Library W5500 Ethernet (`arduino-libraries/Ethernet`): +40–60 KB → tetap < 80%
- Web UI improvements: +10–20 KB → aman
- TOU history runtime: +2–5 KB code → tidak signifikan

### Yang harus dihindari

- TLS certificate bundle (full CA store): +100–200 KB
- ArduinoJson full library: +30–50 KB (gunakan manual JSON string builder yang sudah ada)
- LittleFS + SPIFFS aktif bersamaan: tidak perlu, NVS sudah cukup untuk kebutuhan ini

---

## 4. Backup Scenario: TOU kWh 7 Hari

### Latar belakang

Requirement: simpan data kWh selama 7 hari sebagai backup saat koneksi MQTT gagal.
Data lain (V, I, P, Hz) boleh hilang jika gagal kirim — tidak kritis.

Mengikuti struktur tarif PLN:
- **WBP (Waktu Beban Puncak)**: 18:00–22:00 (configurable)
- **LWBP (Luar Waktu Beban Puncak)**: 00:00–18:00 dan 22:00–24:00
- `total = wbp + lwbp` (selalu konsisten)

### Skenario yang dibuang sebelum sampai sini

| Opsi | Storage | Alasan dibuang |
|------|---------|----------------|
| Semua telemetri (V,I,P,Hz,kWh) per menit | 137 KB (LittleFS) | Overkill, butuh partition change |
| kWh saja per menit | 80 KB (LittleFS) | Masih butuh LittleFS |
| kWh saja per jam | 1.3 KB (NVS) | Lebih dari cukup tapi tidak aligned ke PLN |
| **kWh TOU per hari (dipilih)** | **112 bytes (NVS)** | Minimal, sesuai struktur billing PLN |

### Struktur data

```cpp
struct DailyTouRecord {
    uint32_t date_key;   // YYYYMMDD, misal 20260616
    uint32_t total_wh;   // total Wh hari itu
    uint32_t wbp_wh;     // Wh selama WBP (18:00–22:00)
    uint32_t lwbp_wh;    // Wh selama LWBP
};                       // 16 bytes per hari

struct TouHistoryRuntime {
    DailyTouRecord days[7];   // [0] = hari ini, [6] = 7 hari lalu
    float    baseline_wh;     // meter Wh saat tengah malam (reset daily)
    float    wbp_baseline;    // meter Wh saat masuk WBP
    bool     in_wbp;          // apakah sedang dalam periode WBP
    bool     dirty;           // perlu persist ke NVS
    uint32_t last_persist_ms;
};
```

**Total NVS usage: 112 bytes data + overhead key ~150 bytes = < 300 bytes.**

### NVS namespace

```
Namespace: "tou_history"
Key: "days"  → putBytes(days, 7 × 16 = 112 bytes)
Key: "bline" → putFloat(baseline_wh)
Key: "wbase" → putFloat(wbp_baseline)
```

### Logika runtime

```
Tiap loop():
  nowHour = jam dari RTC

  [Deteksi masuk WBP]
  if nowHour == wbp_start_hour && !in_wbp:
    wbp_baseline = meter.energy_wh
    in_wbp = true

  [Deteksi keluar WBP]
  if nowHour == wbp_end_hour && in_wbp:
    days[0].wbp_wh += meter.energy_wh - wbp_baseline
    in_wbp = false

  [Update running total]
  days[0].total_wh = meter.energy_wh - baseline_wh
  days[0].lwbp_wh  = days[0].total_wh - days[0].wbp_wh

  [Tengah malam rollover]
  if hari berubah:
    shift days[1..6] = days[0..5]
    days[0] = { date_key=today, total=0, wbp=0, lwbp=0 }
    baseline_wh = meter.energy_wh

  [Persist]
  if dirty && nowMs - last_persist_ms > 30000:
    saveToNvs()
    dirty = false
```

### MQTT payload saat reconnect

Saat MQTT reconnect, kirim seluruh 7-hari TOU history sekali ke topic:
```
sems/history/tou
```

Payload:
```json
{
  "uid": "SEMS-83D1B4",
  "days": [
    { "date": "20260616", "total_kwh": 12.3, "wbp_kwh": 4.1, "lwbp_kwh": 8.2 },
    { "date": "20260615", "total_kwh": 11.8, "wbp_kwh": 3.9, "lwbp_kwh": 7.9 },
    ...
  ]
}
```

Simpan juga `pending_tou_send = true` di NVS agar setelah power cycle, data tetap dikirim ulang saat koneksi kembali.

### WBP jam — configurable

Default: start=18, end=22 (PLN umum).
Disimpan di `SystemConfig` atau `HistoryConfig` sebagai `wbp_start_hour` dan `wbp_end_hour`.
Bisa diubah dari web UI untuk menyesuaikan tarif PLN per pelanggan (B2 vs I3 bisa beda).

---

## 5. NVS Budget Keseluruhan

| Namespace | Isi | Estimasi size |
|-----------|-----|---------------|
| `device` | DeviceConfig | ~200 bytes |
| `mqtt` | MqttConfig | ~350 bytes |
| `modbus` | ModbusConfig | ~100 bytes |
| `protection` | ProtectionConfig | ~80 bytes |
| `display` | DisplayConfig | ~60 bytes |
| `history` | HistoryConfig | ~30 bytes |
| `system` | SystemConfig | ~150 bytes |
| `history_rt` | daily[7] float | ~60 bytes |
| `tou_history` | DailyTouRecord[7] | ~300 bytes |
| `network` | WiFi SSID/pass | ~130 bytes |
| **Total** | | **~1.5 KB** |

NVS partition = 20 KB. Usage < 10% — sangat aman.
