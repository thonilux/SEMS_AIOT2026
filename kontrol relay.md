Viewed main.cpp:795-840
Viewed main.cpp:595-625

Berikut adalah dokumentasi teknis standar integrasi MQTT 4-Channel Relay Control yang siap Anda salin dan kirimkan (*forward*) langsung ke **Website Engineer / Backend Developer** Anda:

---

# 📡 Dokumentasi Integrasi MQTT: 4-Channel Relay Control & Monitoring
**Device ID / Prefix**: `trofis/enms/nocola_1`  
**Protokol**: MQTT (QoS 1, Retained State Enabled)

---

## 1. Summary Topik MQTT

| Fungsi | Topik MQTT | Aksi Web Server / Backend |
| :--- | :--- | :--- |
| **Kirim Perintah Relay** | `trofis/enms/nocola_1/cmd` | **PUBLISH** (Kirim Perintah dari Web/Server ke ESP32) |
| **Terima Feedback Status** | `trofis/enms/nocola_1/control-state` | **SUBSCRIBE** (Dengarkan Validasi Status dari ESP32) |

---

## 2. Tabel Perintah Relay (Publish ke Topik `trofis/enms/nocola_1/cmd`)

Backend/Website Engineer dapat mengirimkan perintah menggunakan **String Format Ringkas (Disarankan)** atau **JSON Format**. Semua perintah bersifat *case-insensitive*.

### A. Kontrol Channel Individu (Disarankan)

| Target Relay | Aksi | Payload (String Ringkas) | Payload (JSON Alternative) | Keterangan |
| :--- | :--- | :--- | :--- | :--- |
| **Relay 1** | ON | `R1=1` | `{"R1": 1}` | Menyala / Energize Relay 1 |
| **Relay 1** | OFF | `R1=0` | `{"R1": 0}` | Mati / De-energize Relay 1 |
| **Relay 2** | ON | `R2=1` | `{"R2": 1}` | Menyala / Energize Relay 2 |
| **Relay 2** | OFF | `R2=0` | `{"R2": 0}` | Mati / De-energize Relay 2 |
| **Relay 3** | ON | `R3=1` | `{"R3": 1}` | Menyala / Energize Relay 3 |
| **Relay 3** | OFF | `R3=0` | `{"R3": 0}` | Mati / De-energize Relay 3 |
| **Relay 4** | ON | `R4=1` | `{"R4": 1}` | Menyala / Energize Relay 4 (Master) |
| **Relay 4** | OFF | `R4=0` | `{"R4": 0}` | Mati / De-energize Relay 4 (Master) |

### B. Kontrol Masal (Bulk Control)

| Target Relay | Aksi | Payload (String) | Payload (JSON Alternative) | Keterangan |
| :--- | :--- | :--- | :--- | :--- |
| **Semua Relay** | ALL ON | `SET_RELAY` | `{"action": "SET_RELAY"}` | Menyala seluruh channel R1 - R4 |
| **Semua Relay** | ALL OFF | `RESET_RELAY` | `{"action": "RESET_RELAY"}` | Mati seluruh channel R1 - R4 |

---

## 3. Spesifikasi Payload Feedback (Subscribe di Topik `trofis/enms/nocola_1/control-state`)

Setiap kali status relay berubah atau terjadi event proteksi, ESP32 akan mempublikasikan data JSON validasi status terkini secara otomatis.

### Structure Payload JSON:
```json
{
  "timestamp": "2026-08-05T01:44:00",
  "r1": 1,
  "r2": 0,
  "r3": 0,
  "r4": 0
}
```

### Arti & Penjelasan Kode Status (`r1`, `r2`, `r3`, `r4`):

| Nilai State (`int`) | Label Status | Warna UI (Rekomendasi) | Deskripsi Teknis |
| :---: | :--- | :--- | :--- |
| `0` | **OFF** | ⚪ Gray / Red | Relay mati / terputus. Jalur listrik mati. |
| `1` | **ON** | 🟢 Green | Relay aktif / menyala. Jalur listrik terhubung. |
| `2` | **TRIP** | 🔴 Red / Flashing | **Proteksi Overcurrent Aktif**. Relay otomatis dimatikan oleh ESP32 karena arus melebihi limit. |

---

## 4. Mekanisme & Trigger Pengiriman Feedback dari ESP32

Backend Website Engineer **wajib** mendengarkan (*subscribe*) topik `trofis/enms/nocola_1/control-state` untuk meng-update tampilan sakelar di Web Dashboard secara real-time.

ESP32 akan otomatis mengirimkan payload feedback pada kondisi berikut:
1. **Respon Perintah**: Setelah menerima perintah `cmd` dari MQTT server/postman.
2. **Lokal Web UI**: Setelah pengguna mengubah sakelar dari Web UI lokal ESP32.
3. **Event Overcurrent Trip**: Saat arus listrik melebihi batas limit dan hardware memicu *Trip*.
4. **Booting / Power Restoration**: Saat ESP32 pertama kali dinyalakan dan sukses terhubung ke MQTT Broker (sinkronisasi awal).
5. **Auto Reconnect**: Saat koneksi internet/broker pulih setelah sempat terputus.

---

## 5. Contoh Snippet Implementasi (Node.js / MQTT Client)

```javascript
const mqtt = require('mqtt');
const client = mqtt.connect('mqtt://broker.hivemq.com:1883');

const TOPIC_CMD = 'trofis/enms/nocola_1/cmd';
const TOPIC_STATE = 'trofis/enms/nocola_1/control-state';

client.on('connect', () => {
  console.log('Connected to MQTT Broker');
  // Subscribe ke topik feedback untuk update state di database/UI website
  client.subscribe(TOPIC_STATE);
});

// A. Fungsi untuk Mengirim Perintah dari Website ke ESP32
function controlRelay(relayIndex, turnOn) {
  const payload = `R${relayIndex}=${turnOn ? 1 : 0}`;
  client.publish(TOPIC_CMD, payload, { qos: 1 });
  console.log(`Command sent: ${payload}`);
}

// B. Handler Menerima Feedback Validasi State dari ESP32
client.on('message', (topic, message) => {
  if (topic === TOPIC_STATE) {
    const data = JSON.parse(message.toString());
    console.log('Realtime Relay Status from ESP32:', data);
    
    // Update status di UI Website berdasarkan nilai data.r1, data.r2, dst.
    // 0 = OFF, 1 = ON, 2 = TRIP
  }
});
```