# 📡 Dokumentasi Integrasi MQTT: 4-Channel Relay Control & Monitoring

**Device ID / Prefix**: `trofis/enms/nocola_1` (`<base_topic>/<device_name>`, dikonfigurasi di halaman `/mqtt` & `/device`)
**Protokol**: MQTT — **QoS 0 saja** (lihat catatan di bagian 6)
**Target Hardware**: ESP32 SEMS AIoT Firmware 2026

Ada **dua cara** mengontrol relay lewat MQTT. Boleh dipakai bersamaan — keduanya aktif terus, backend bebas pilih salah satu atau keduanya:

- **A. Per-Relay Boolean Topic** (`control/switch_N`) — direkomendasikan untuk integrasi web sederhana. Payload selalu 1 karakter.
- **B. Topik `cmd` gabungan** (string ringkas / JSON) — cocok untuk automation/bulk control.

---

## 1. Per-Relay Boolean Switch Topic (Direkomendasikan)

### 1.1 Kirim Perintah (Web/Backend → Device)

| Fungsi          | Topik MQTT                              | Aksi        |
| :-------------- | :-------------------------------------- | :---------- |
| Kontrol Relay 1 | `trofis/enms/nocola_1/control/switch_1` | **PUBLISH** |
| Kontrol Relay 2 | `trofis/enms/nocola_1/control/switch_2` | **PUBLISH** |
| Kontrol Relay 3 | `trofis/enms/nocola_1/control/switch_3` | **PUBLISH** |
| Kontrol Relay 4 | `trofis/enms/nocola_1/control/switch_4` | **PUBLISH** |

Payload **wajib tepat 1 karakter**:

| Payload | Aksi                                                            |
| :-----: | :-------------------------------------------------------------- |
|   `1`   | Relay ON                                                        |
|   `0`   | Relay OFF                                                       |
| lainnya | Diabaikan (dicatat di serial log sebagai "non-boolean payload") |

Device mencocokkan topik secara **exact match** — payload tidak diparsing sebagai teks bebas seperti pada topik `cmd`.

Contoh:

```bash
mosquitto_pub -h 128.199.206.166 -p 1883 -u labiot -P 'iotlabftuns2023' \
  -t 'trofis/enms/nocola_1/control/switch_1' -m "1" -q 1
```

### 1.2 Terima Feedback Status (Device → Web/Backend)

| Fungsi         | Topik MQTT                                    | Aksi          |
| :------------- | :-------------------------------------------- | :------------ |
| Status Relay 1 | `trofis/enms/nocola_1/control/switch_1/state` | **SUBSCRIBE** |
| Status Relay 2 | `trofis/enms/nocola_1/control/switch_2/state` | **SUBSCRIBE** |
| Status Relay 3 | `trofis/enms/nocola_1/control/switch_3/state` | **SUBSCRIBE** |
| Status Relay 4 | `trofis/enms/nocola_1/control/switch_4/state` | **SUBSCRIBE** |

Payload **1 karakter**, retained:

| Payload | Arti                                          |
| :-----: | :-------------------------------------------- |
|   `0`   | OFF                                           |
|   `1`   | ON                                            |
|   `2`   | TRIP (proteksi overcurrent/stale-meter aktif) |

Contoh:

```bash
mosquitto_sub -h 128.199.206.166 -p 1883 -u user_1 -P iotlabftuns2023 \
  -t "trofis/enms/nocola_1/control/switch_1/state"
```

---

## 2. Topik `cmd` Gabungan (Alternatif Bulk/Automation)

### 2.1 Kirim Perintah (Publish ke `trofis/enms/nocola_1/cmd`)

Semua perintah _case-insensitive_, boleh string ringkas atau JSON.

**A. Kontrol channel individu**

| Target Relay | Aksi | Payload (String) | Payload (JSON) |
| :----------- | :--- | :--------------- | :------------- |
| Relay 1      | ON   | `R1=1`           | `{"R1": 1}`    |
| Relay 1      | OFF  | `R1=0`           | `{"R1": 0}`    |
| Relay 2      | ON   | `R2=1`           | `{"R2": 1}`    |
| Relay 2      | OFF  | `R2=0`           | `{"R2": 0}`    |
| Relay 3      | ON   | `R3=1`           | `{"R3": 1}`    |
| Relay 3      | OFF  | `R3=0`           | `{"R3": 0}`    |
| Relay 4      | ON   | `R4=1`           | `{"R4": 1}`    |
| Relay 4      | OFF  | `R4=0`           | `{"R4": 0}`    |

**B. Kontrol massal (bulk)**

| Aksi    | Payload (String) | Payload (JSON)              |
| :------ | :--------------- | :-------------------------- |
| ALL ON  | `SET_RELAY`      | `{"action": "SET_RELAY"}`   |
| ALL OFF | `RESET_RELAY`    | `{"action": "RESET_RELAY"}` |

Ada juga alias legacy topik `trofis/enms/nocola_1/relay/set` yang menerima payload sama seperti `cmd`.

### 2.2 Terima Feedback (Subscribe ke `trofis/enms/nocola_1/control-state`)

Payload JSON, dikirim setiap kali salah satu relay berubah status:

```json
{
  "timestamp": "2026-08-05 01:44:00",
  "r1": 1,
  "r2": 0,
  "r3": 0,
  "r4": 0
}
```

| Nilai (`int`) | Label | Deskripsi                                                        |
| :-----------: | :---- | :--------------------------------------------------------------- |
|      `0`      | OFF   | Relay mati                                                       |
|      `1`      | ON    | Relay aktif                                                      |
|      `2`      | TRIP  | Proteksi overcurrent/stale-meter aktif, relay otomatis dimatikan |

---

## 3. Kapan Feedback Dikirim

Device mempublikasikan **kedua** bentuk feedback (per-switch `.../state` di bagian 1.2 dan JSON `control-state` di bagian 2.2) bersamaan, setiap kali:

1. Menerima perintah lewat topik `cmd`, `relay/set`, atau `control/switch_N`.
2. Relay diubah dari Web UI lokal ESP32.
3. Proteksi overcurrent / meter-stale memicu TRIP.
4. Relay auto-retry lolos dari TRIP lockout.
5. Booting & berhasil connect ke broker (sinkronisasi awal).
6. Auto-reconnect setelah koneksi broker sempat putus.

---

## 4. Prasyarat Agar Perintah ON Diterima

Semua jalur perintah (baik `switch_N` maupun `cmd`) tunduk pada aturan proteksi yang sama di firmware:

- **Relay Output System** harus **enabled** di halaman `/protection` — kalau tidak, semua perintah diabaikan dan relay dipaksa OFF.
- Kalau relay sedang dalam status **TRIP** dan masih dalam jendela _auto-retry lockout_, perintah ON akan ditolak (dicatat di serial log sebagai "ON blocked") sampai lockout selesai atau kondisi aman kembali.
- Perintah OFF selalu diterima, kapan pun.

---

## 5. Contoh Snippet Implementasi (Node.js / MQTT Client)

```javascript
const mqtt = require("mqtt");
const client = mqtt.connect("mqtt://128.199.206.166:1883", {
  username: "labiot",
  password: "iotlabftuns2023",
});

const PREFIX = "trofis/enms/nocola_1";

client.on("connect", () => {
  console.log("Connected to MQTT Broker");
  // Subscribe ke feedback per-relay (rekomendasi)
  for (let i = 1; i <= 4; i++) {
    client.subscribe(`${PREFIX}/control/switch_${i}/state`);
  }
});

// Kirim perintah ke satu relay (payload harus persis "1" atau "0")
function controlRelay(relayIndex, turnOn) {
  const topic = `${PREFIX}/control/switch_${relayIndex}`;
  const payload = turnOn ? "1" : "0";
  client.publish(topic, payload, { qos: 1 });
  console.log(`Command sent: ${topic} = ${payload}`);
}

// Terima feedback status
client.on("message", (topic, message) => {
  const match = topic.match(/control\/switch_(\d)\/state$/);
  if (match) {
    const relayIndex = match[1];
    const state = message.toString(); // "0" | "1" | "2"
    console.log(`Relay ${relayIndex} state:`, state);
    // 0 = OFF, 1 = ON, 2 = TRIP
  }
});
```

---

## 6. Catatan Penting: QoS

Firmware ESP32 memakai library `PubSubClient`, yang **hanya mendukung QoS 0** — baik untuk `subscribe()` maupun `publish()`. Broker akan tetap menerima publish dari web dengan `-q 1`, tapi pengiriman ke device (dan publish balik dari device) selalu terjadi di QoS 0. Ini biasanya cukup andal di jaringan yang stabil, tapi bukan garansi _at-least-once delivery_ end-to-end seperti QoS 1 sungguhan. Kalau garansi pengiriman jadi kebutuhan keras, ini butuh migrasi library MQTT di firmware (bukan sekadar ubah konfigurasi).
