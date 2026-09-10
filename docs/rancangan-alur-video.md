# Rancangan Alur Video — SEMS AIoT (Dortek 2026)

> Luaran video Program Hilirisasi Riset Prioritas, Skema Dorongan Teknologi 2026.
> Format dokumenter, durasi 3–5 menit, 1080p landscape, MP4/MOV/FLV.
> Voice over + subtitle/running text bahasa Indonesia sepanjang video.
> Pola alur mengikuti contoh praktik baik DRI IPB (Starter Kering Kultur Bakteri):
> latar belakang → masalah → solusi → **proses pengembangan langkah demi langkah** → pengujian → hasil & implementasi → penutup.

---

## 0. Bumper Pembuka (0:00–0:20) — WAJIB

Tampilan diam / motion graphic, sebelum masuk konten:

- **Judul + program:** "Smart Energy Management System (SEMS) berbasis Artificial Intelligence of Things (AIoT)" — Program Hilirisasi Riset Prioritas, Skema Dorongan Teknologi Tahun 2026
- **Logo urut kiri → kanan:** Kemdiktisaintek · DiktiSaintek Berdampak · UNS · Nocola
- **Tim peneliti + NIDN:**
  - Ketua: Ir. Joko Slamet Saputro, S.Pd., M.T. — NIDN 0024048904
  - Ir. Agus Ramelan, S.Pd., M.T. — NIDN 0015039201
  - Ir. Meiyanto Eko Sulistyo, S.T., M.Eng. — NIDN 0013057705
  - Dr. Ir. Chico Hermanu Brillianto Apribowo, S.T., M.Eng. — NIDN 0016048802

Voice over: identitas program dibacakan singkat, nada institusional.

---

## 1. Intro & Latar Belakang (0:20–0:55)

**Visual:** b-roll gedung kampus/perkantoran malam hari lampu menyala, panel listrik, meteran PLN berputar, ruang kerja full AC, koridor lampu menyala tanpa orang.

**Narasi (VO):**
> Energi listrik adalah tulang punggung operasional gedung modern — kampus, kantor pemerintahan, hotel, hingga rumah sakit. Namun konsumsi energi terus meningkat setiap tahun, sementara sebagian besar pengelola gedung tidak memiliki gambaran nyata ke mana daya listrik mereka mengalir.

**Running text:** poin data konsumsi energi bangunan (isi angka valid saat produksi).

---

## 2. Masalah (0:55–1:25)

**Visual:** tagihan listrik menumpuk, orang mencatat meteran manual pakai kertas, panel listrik tanpa alat ukur, grafik beban naik, AC & lampu menyala di ruang kosong.

**Narasi (VO):**
> Pemantauan energi masih dilakukan manual dan hanya sebulan sekali saat tagihan datang. Tidak ada data real-time. Pemborosan tidak terdeteksi. Anomali beban — kabel panas, beban berlebih, peralatan rusak — baru diketahui setelah terjadi gangguan. Pengelola gedung seolah "buta" terhadap sistem kelistrikannya sendiri.

**Running text:** 3 titik nyeri — *tidak real-time · tidak ada deteksi dini · tidak ada kendali jarak jauh*.

---

## 3. Solusi: SEMS AIoT (1:25–1:55)

**Visual:** reveal produk — koper demo SEMS dibuka, perangkat gateway ESP32, layar OLED menyala, dashboard live di laptop/HP, diagram arsitektur sederhana (meter → RS485 → gateway → MQTT → cloud/dashboard → AI).

**Narasi (VO):**
> Smart Energy Management System berbasis AIoT hadir sebagai jawabannya. Sebuah gateway cerdas membaca hingga empat power meter sekaligus melalui satu jalur komunikasi RS485/Modbus, mengirim data ke server secara terjadwal presisi, menampilkannya dalam dashboard real-time, dan memanfaatkan kecerdasan buatan untuk mengenali pola konsumsi serta memprediksi kebutuhan energi.

**Running text:** *Monitor real-time · Kendali jarak jauh · Proteksi otomatis · Prediksi berbasis AI*.

---

## 4. Fitur & Keunggulan (1:55–2:25)

**Visual:** screen recording web UI (Home, Network, MQTT, Modbus, Relay, System), close-up OLED navigasi 1 tombol, relay board 4 channel klik ON/OFF, notifikasi trip overcurrent, proses OTA update.

**Narasi (VO):** disampaikan mengalir, bukan bacaan daftar:
> Satu perangkat membaca beragam merek meter — Schneider maupun Renata — pada bus yang sama. Jalur jaringan gandanya, Ethernet sebagai utama dan WiFi sebagai cadangan, berpindah otomatis saat kabel terputus. Board relai empat kanal dapat dikendalikan dari web maupun perintah MQTT, lengkap dengan proteksi arus lebih yang memutus beban secara otomatis. Konfigurasi di lokasi cukup lewat mode setup nirkabel, dan pembaruan perangkat lunak dilakukan dari jarak jauh tanpa membuka perangkat.

**Running text (chip berurutan):** *Multi-meter 1 bus · Ethernet + WiFi failover · Relay 4-channel + proteksi · Jadwal publish per-meter · Config Mode AP · OTA update · OLED 1-tombol*.

---

## 5. Proses Pelaksanaan & Pengembangan Produk (2:25–3:40) — INTI DOKUMENTER

> Segmen wajib: perancangan PCB, assembly, perakitan, uji coba. Ditampilkan **langkah demi langkah**, gaya proses seperti contoh IPB.

| # | Tahap | Visual b-roll | Narasi singkat (VO) |
|---|-------|---------------|---------------------|
| 5.1 | Perancangan skematik & PCB | layar software EDA, penarikan jalur PCB, render 3D board | "Pengembangan diawali dari perancangan skematik dan tata letak PCB gateway." |
| 5.2 | Fabrikasi & kedatangan PCB | PCB kosong hasil fabrikasi, inspeksi visual | "Papan hasil fabrikasi diperiksa sebelum masuk tahap perakitan." |
| 5.3 | Assembly komponen | penyolderan komponen, pemasangan modul ESP32, W5500, transceiver RS485, konektor | "Komponen dipasang dan disolder — mikrokontroler, modul Ethernet, antarmuka RS485, dan terminal." |
| 5.4 | Perakitan unit / enclosure | pemasangan board ke enclosure/koper, wiring ke terminal meter & relay, pemasangan OLED + tombol | "Seluruh modul dirakit ke dalam unit, termasuk pengkabelan ke power meter dan board relai." |
| 5.5 | Flashing firmware | proses upload firmware via USB, serial monitor, `pio run -t upload` | "Perangkat lunak ditanamkan ke perangkat dan diverifikasi lewat serial monitor." |
| 5.6 | Konfigurasi awal | sambung ke AP `SEMS-SETUP-XXXX`, isi form Network/MQTT/Modbus di web UI | "Konfigurasi awal dilakukan melalui mode setup nirkabel bawaan perangkat." |
| 5.7 | Bring-up di meja uji | gateway menyala, OLED tampil status, meter terbaca, data muncul di dashboard | "Setelah dinyalakan, perangkat langsung membaca meter dan menampilkan data." |

**Musik:** instrumental proses (mirip segmen fermentasi contoh IPB), VO tetap ada di tiap langkah.

---

## 6. Pengujian (3:40–4:15)

> Padanan "pengujian rutin 6 minggu" pada contoh IPB.

**Visual:** setup uji di lab — sumber beban / meter referensi, tabel hasil pembacaan, grafik selisih pembacaan vs referensi, uji relay memutus beban, uji failover cabut kabel LAN, uji OTA, log MQTT broker menerima data terjadwal.

| Uji | Yang dibuktikan |
|-----|-----------------|
| Akurasi pembacaan meter | Selisih pembacaan gateway vs meter referensi dalam batas toleransi |
| Multi-meter round-robin | 4 meter terbaca bergantian tanpa tabrakan di 1 bus RS485 |
| Jadwal publish MQTT | Data `elc_data` & `elc_wh` terkirim tepat sesuai interval per-meter (berbasis RTC/NTP) |
| Failover jaringan | Cabut kabel Ethernet → pindah ke WiFi otomatis, data tidak putus |
| Proteksi relai | Arus melewati ambang → relai trip otomatis, opsi auto-retry |
| Write-register (FC06) | Ubah rasio CT/PT & Digital Output aman, dengan verifikasi read-back |
| OTA update | Firmware baru diunggah via web, perangkat reboot dengan versi baru |

**Narasi (VO):**
> Perangkat diuji menyeluruh: akurasi pembacaan dibandingkan meter referensi, ketepatan jadwal pengiriman data, perpindahan jalur jaringan otomatis, kerja proteksi relai, hingga pembaruan perangkat lunak dari jarak jauh. Seluruh fungsi berjalan sesuai rancangan.

---

## 7. Implementasi di Berbagai Sektor (4:15–4:40)

**Visual:** montage 4 sektor — gedung universitas, kantor pemerintahan, hotel, rumah sakit — dengan overlay dashboard SEMS di tiap lokasi (boleh mockup/ilustrasi bila belum ada instalasi nyata; jangan klaim palsu).

**Narasi (VO):**
> SEMS AIoT dirancang untuk diterapkan lintas sektor — gedung perkuliahan universitas, kantor pemerintahan, perhotelan, dan rumah sakit — di mana pemantauan energi yang akurat dan kendali beban jarak jauh memberi dampak langsung pada efisiensi biaya dan keandalan operasional.

**Running text:** capaian TKT yang ditargetkan (isi level sesuai proposal).

---

## 8. Contact Us (4:40–4:50)

**Visual:** kartu kontak — logo Nocola / Lab IoT FT UNS, alamat, email, website, QR.

**Narasi (VO):** ajakan kolaborasi/adopsi singkat.

---

## 9. Penutup: Ucapan Terima Kasih & Credit (4:50–5:00) — WAJIB

**Visual:** credit roll.

**Teks credit (wajib tercantum):**

> Penelitian ini didanai oleh Direktorat Hilirisasi dan Kemitraan, Direktorat Jenderal Riset dan Pengembangan, Kementerian Pendidikan Tinggi, Sains, dan Teknologi.
>
> Nomor Kontrak: 108/SPK/C.C4/PPK.DHK/IV/2026
> Nomor Perjanjian Penugasan: 476.1/UN27.22/PT.01.03/2026
>
> Terima kasih kepada:
> Direktorat Hilirisasi dan Kemitraan, Direktorat Jenderal Riset dan Pengembangan, Kementerian Pendidikan Tinggi, Sains, dan Teknologi
> LPPM Universitas Sebelas Maret
>
> Tim Peneliti:
> Ir. Joko Slamet Saputro, S.Pd., M.T. (Ketua)
> Ir. Agus Ramelan, S.Pd., M.T.
> Ir. Meiyanto Eko Sulistyo, S.T., M.Eng.
> Dr. Ir. Chico Hermanu Brillianto Apribowo, S.T., M.Eng.
>
> Segenap Tim:
> Hisbullah Ahmad Fathoni, S.T.
> Mario Alfandi Wirawan, S.T.
> Muhammad Ilham Alghifari, S.T.
> Laboratorium IoT FT UNS

---

## Checklist Kepatuhan Ketentuan

- [ ] Bumper awal: judul + program + 4 logo urut benar + nama tim + NIDN
- [ ] Durasi 3–5 menit
- [ ] Bentuk dokumenter (bukan pidato/ceramah, bukan kumpulan foto/PowerPoint)
- [ ] Menampilkan perancangan PCB, assembly, perakitan, uji coba
- [ ] Menunjukkan fungsi & implementasi hasil produk
- [ ] Alur konten sesuai: Intro → Solusi → Fitur → Implementasi multi-sektor → Contact us → Penutup
- [ ] Menggambarkan hasil keseluruhan sesuai target TKT
- [ ] Resolusi minimal 1080p, aspek rasio landscape
- [ ] Format MP4 / QuickTime MOV / FLV
- [ ] Narasi deskriptif: subtitle bahasa Indonesia (running text) + voice over
- [ ] Semua konten hasil karya tim pengusul
- [ ] Hormati copyright / HKI (musik & aset berlisensi bebas atau milik sendiri)
- [ ] Tanpa unsur politik, iklan, SARA
- [ ] Credit title lengkap + nomor kontrak + nomor perjanjian penugasan
- [ ] Storyboard dilampirkan bersama video

---

## Naskah Voice Over Lengkap + Estimasi Durasi

> Gaya: santai, ngobrol, ramah ke orang awam. Kalimat pendek. Boleh sapa "Anda".
> Hindari istilah teknis mentah di VO — kalau muncul, langsung dijelaskan pakai analogi.
> Istilah seperti RS485, Modbus, MQTT cukup ditampilkan di running text/visual, bukan diucapkan berat.
> Tempo baca acuan: ~150 kata/menit (tenang, ada jeda antar kalimat).

---

### Panduan Notasi Jeda (untuk prompt AI TTS)

Naskah di bawah pakai penanda jeda yang gampang di-search-replace ke sintaks TTS mana pun.

| Penanda | Arti | Perkiraan | Kapan dipakai |
|---------|------|-----------|---------------|
| `[jeda-pendek]` | tarik napas tipis | 0.3–0.4 dtk | antar anak kalimat, setelah koma penting |
| `[jeda]` | jeda kalimat normal | 0.6–0.8 dtk | antar kalimat |
| `[jeda-panjang]` | ganti pikiran / pindah adegan | 1.0–1.5 dtk | antar paragraf, sebelum poin baru |
| `[napas]` | ambil napas terdengar | 0.5 dtk | sebelum kalimat panjang |
| `[tekan: kata]` | tekankan kata itu | — | kata kunci yang harus menonjol |
| `[pelan]` ... `[/pelan]` | perlambat tempo | — | bagian penting / angka / nama lembaga |

**Cara pakai per engine:**

- **ElevenLabs** — ganti `[jeda-pendek]` menjadi `<break time="0.4s" />`, `[jeda]` menjadi `<break time="0.7s" />`, `[jeda-panjang]` menjadi `<break time="1.2s" />`. `[napas]` menjadi `<break time="0.5s" />`. Hapus tag `[tekan:]`/`[pelan]` — atur lewat slider Stability/Style, atau tulis huruf kapital untuk penekanan ringan.
- **OpenAI TTS / gpt-4o-mini-tts** — hapus semua tag, ganti `[jeda]` dengan tanda titik + baris baru; kendalikan tempo lewat instruksi prompt ("baca santai, ada jeda antar kalimat, nada ramah").
- **Google Cloud TTS (SSML)** — `[jeda-pendek]` → `<break time="300ms"/>`, `[jeda]` → `<break time="700ms"/>`, `[jeda-panjang]` → `<break time="1200ms"/>`. `[pelan]...[/pelan]` → `<prosody rate="90%">...</prosody>`. `[tekan: X]` → `<emphasis level="strong">X</emphasis>`. Bungkus tiap segmen dengan `<speak>...</speak>`.
- **Azure Speech (SSML)** — sama seperti Google; `[pelan]` → `<prosody rate="-10%">`.
- **Coqui / Piper / TTS lokal** — hapus semua tag, pecah jadi 1 kalimat per baris; jeda dihasilkan dari tanda baca. `[jeda-panjang]` → tambahkan baris kosong / titik ganda.

**Prompt gaya (tempel di system/instruction TTS):**
> "Baca sebagai narator dokumenter Indonesia yang santai dan ramah, seperti menjelaskan ke teman. Tempo sedang, tidak buru-buru. Beri jeda jelas antar kalimat. Nada hangat dan meyakinkan, bukan formal kaku. Bagian nama lembaga dan angka dibaca sedikit lebih pelan dan jelas."

**Voice:** pilih 1 voice Indonesia natural, laki-laki atau perempuan, konsisten untuk seluruh 10 segmen. Rekam per segmen (file terpisah `vo_seg0.wav` … `vo_seg9.wav`) supaya gampang re-take.

---

### Seg 0 — Bumper Pembuka
**VO (dengan penanda jeda):**
> Ini [tekan: SEMS] — Smart Energy Management System. [jeda]
> Sistem pintar untuk memantau dan mengatur pemakaian listrik gedung. [jeda-panjang]
> [pelan] Karya tim Universitas Sebelas Maret bersama Nocola, [jeda-pendek] lewat Program Dorongan Teknologi tahun dua ribu dua puluh enam. [/pelan]

Kata: ~35 · Baca: ~14 dtk · **Segmen: 0:00–0:18 (18 dtk)** — sisanya motion graphic logo & nama tim tanpa VO.

### Seg 1 — Intro & Latar Belakang
**VO (dengan penanda jeda):**
> Coba lihat sekeliling gedung mana pun — [jeda-pendek] kampus, [jeda-pendek] kantor, [jeda-pendek] hotel, [jeda-pendek] rumah sakit. [jeda]
> Semua jalan karena listrik. [jeda-panjang]
> [napas] Masalahnya, [jeda-pendek] tagihan listrik terus naik tiap tahun. [jeda]
> Tapi kalau ditanya, [jeda-pendek] "listriknya habis ke mana aja?" [jeda-pendek] — jarang ada yang bisa jawab dengan pasti.

Kata: ~45 · Baca: ~18 dtk · **Segmen: 0:18–0:53 (35 dtk)**.

### Seg 2 — Masalah
**VO (dengan penanda jeda):**
> Selama ini, [jeda-pendek] cara ngeceknya masih manual. [jeda]
> Petugas keliling, [jeda-pendek] catat angka meteran di kertas, [jeda-pendek] sebulan sekali. [jeda]
> Itu pun baru dilihat pas tagihan datang. [jeda-panjang]
> [napas] Jadi nggak ada data yang benar-benar [tekan: sekarang]. [jeda]
> Kalau ada yang boros, [jeda-pendek] nggak ketahuan. [jeda]
> Kalau ada yang mulai bermasalah — [jeda-pendek] kabel kepanasan, [jeda-pendek] beban kelebihan — [jeda-pendek] biasanya baru sadar setelah listrik ngadat. [jeda-panjang]
> Singkatnya: [jeda-pendek] pengelola gedung kayak nyetir mobil [jeda-pendek] tapi kaca depannya ketutup.

Kata: ~65 · Baca: ~26 dtk · **Segmen: 0:53–1:27 (34 dtk)**.

### Seg 3 — Solusi: SEMS AIoT
**VO (dengan penanda jeda):**
> Nah, [jeda-pendek] di sinilah SEMS masuk. [jeda-panjang]
> Alat ini dipasang di panel listrik, [jeda-pendek] lalu "membaca" meteran-meteran yang sudah ada — [jeda-pendek] sampai empat meteran sekaligus lewat satu kabel. [jeda]
> Datanya langsung dikirim ke server [jeda-pendek] dan tampil di layar HP atau komputer, [jeda-pendek] [tekan: real-time]. [jeda-panjang]
> [napas] Dan bukan cuma nampilin angka. [jeda]
> Ada kecerdasan buatan yang belajar pola pemakaian Anda, [jeda-pendek] lalu bantu memperkirakan kebutuhan listrik ke depan.

Kata: ~60 · Baca: ~24 dtk · **Segmen: 1:27–1:59 (32 dtk)**.

### Seg 4 — Fitur & Keunggulan
**VO (dengan penanda jeda):**
> Beberapa hal yang bikin SEMS praktis. [jeda-panjang]
> Satu alat bisa baca meteran dari merek yang beda-beda, [jeda-pendek] di jalur yang sama. [jeda-panjang]
> [napas] Koneksinya dobel — [jeda-pendek] pakai kabel LAN sebagai jalur utama, [jeda-pendek] dan WiFi sebagai cadangan. [jeda]
> Kalau kabelnya lepas, [jeda-pendek] otomatis pindah ke WiFi, [jeda-pendek] datanya tetap jalan. [jeda-panjang]
> [napas] Ada juga empat sakelar pintar [jeda-pendek] yang bisa dinyalain atau dimatiin dari jauh, [jeda-pendek] lewat aplikasi. [jeda]
> Kalau arusnya kelebihan, [jeda-pendek] alat ini langsung memutus sendiri [jeda-pendek] supaya aman. [jeda-panjang]
> Mau setting? [jeda-pendek] Cukup dari HP, [jeda-pendek] nggak perlu bongkar alat. [jeda]
> Mau update software? [jeda-pendek] Bisa dari jarak jauh.

Kata: ~85 · Baca: ~34 dtk · **Segmen: 1:59–2:38 (39 dtk)**.

### Seg 5 — Proses Pelaksanaan & Pengembangan (inti dokumenter)
**VO (per langkah, ada jeda musik proses di antaranya):**
> Alat ini nggak jadi dalam semalam. [jeda-pendek] Ini prosesnya. [jeda-panjang]
> Dimulai dari mendesain papan rangkaiannya — [jeda-pendek] nentuin jalur dan tata letak komponen di komputer. [jeda-panjang]
> Papan yang sudah dicetak [jeda-pendek] lalu diperiksa satu-satu sebelum dirakit. [jeda-panjang]
> Komponennya dipasang dan disolder — [jeda-pendek] otak alatnya, [jeda-pendek] modul jaringan, [jeda-pendek] dan konektor-konektornya. [jeda-panjang]
> Semua bagian digabung jadi satu unit, [jeda-pendek] lengkap sama kabel ke meteran dan ke sakelarnya. [jeda-panjang]
> Software-nya dimasukin ke alat, [jeda-pendek] terus dicek jalan atau nggak. [jeda-panjang]
> Setting awal dilakukan lewat HP — [jeda-pendek] sambungin ke jaringan, [jeda-pendek] atur server, [jeda-pendek] kenalin meterannya. [jeda-panjang]
> Begitu dinyalain, [jeda-pendek] alat langsung baca meteran [jeda-pendek] dan datanya muncul di layar.

Kata: ~95 · Baca: ~38 dtk · + ruang b-roll proses ~28 dtk · **Segmen: 2:38–3:44 (66 dtk)**.

### Seg 6 — Pengujian
**VO (dengan penanda jeda):**
> Sebelum dipakai beneran, [jeda-pendek] alat ini diuji habis-habisan. [jeda-panjang]
> Angkanya dicocokin sama alat ukur standar — [jeda-pendek] hasilnya harus [tekan: pas]. [jeda-panjang]
> Empat meteran dibaca gantian di satu jalur, [jeda-pendek] harus rapi, [jeda-pendek] nggak boleh tabrakan. [jeda-panjang]
> [napas] Kabel LAN-nya sengaja dicabut — [jeda-pendek] dan benar, [jeda-pendek] alat langsung pindah ke WiFi [jeda-pendek] tanpa kehilangan data. [jeda-panjang]
> Sakelar pengamannya diuji, [jeda-pendek] memutus sendiri pas arus kelebihan. [jeda]
> Update software dari jauh juga dicoba [jeda-pendek] sampai alat nyala lagi dengan versi baru. [jeda-panjang]
> Semuanya jalan sesuai rencana.

Kata: ~85 · Baca: ~34 dtk · **Segmen: 3:44–4:20 (36 dtk)**.

### Seg 7 — Implementasi di Berbagai Sektor
**VO (dengan penanda jeda):**
> SEMS bisa dipakai di mana aja yang butuh listrik terpantau rapi — [jeda-pendek] gedung kuliah, [jeda-pendek] kantor pemerintahan, [jeda-pendek] hotel, [jeda-pendek] sampai rumah sakit. [jeda-panjang]
> [napas] Di semua tempat itu, [jeda-pendek] tahu persis pemakaian listrik [jeda-pendek] dan bisa ngatur beban dari jauh [jeda-pendek] artinya biaya lebih hemat [jeda-pendek] dan operasional lebih aman.

Kata: ~45 · Baca: ~18 dtk · **Segmen: 4:20–4:42 (22 dtk)**.

### Seg 8 — Contact Us
**VO (dengan penanda jeda):**
> Tertarik pasang SEMS di gedung Anda? [jeda]
> Yuk, [jeda-pendek] ngobrol. [jeda]
> Kontaknya ada di bawah ini.

Kata: ~15 · Baca: ~6 dtk · **Segmen: 4:42–4:50 (8 dtk)**.

### Seg 9 — Penutup & Credit
**VO (dengan penanda jeda — dibaca formal):**
> [pelan] Penelitian ini didanai oleh [jeda-pendek] Direktorat Hilirisasi dan Kemitraan, [jeda-pendek] Direktorat Jenderal Riset dan Pengembangan, [jeda-pendek] Kementerian Pendidikan Tinggi, Sains, dan Teknologi. [/pelan] [jeda-panjang]
> Terima kasih juga untuk LPPM Universitas Sebelas Maret, [jeda-pendek] dan semua yang terlibat.

Kata: ~40 · Baca: ~16 dtk · + credit roll senyap ~4 dtk · **Segmen: 4:50–5:00 (10 dtk)**.

> Catatan: kalimat pendanaan di Seg 9 tetap dibaca formal — ini bagian wajib, jangan disantaikan.

---

### Naskah Bersih (tanpa tag, cadangan)

Kalau engine TTS-nya menolak tag, pakai versi ini — 1 kalimat per baris, jeda dari tanda baca:

```
[Seg 0]
Ini SEMS — Smart Energy Management System.
Sistem pintar untuk memantau dan mengatur pemakaian listrik gedung.
Karya tim Universitas Sebelas Maret bersama Nocola, lewat Program Dorongan Teknologi tahun dua ribu dua puluh enam.

[Seg 1]
Coba lihat sekeliling gedung mana pun — kampus, kantor, hotel, rumah sakit.
Semua jalan karena listrik.
Masalahnya, tagihan listrik terus naik tiap tahun.
Tapi kalau ditanya, "listriknya habis ke mana aja?" — jarang ada yang bisa jawab dengan pasti.

[Seg 2]
Selama ini, cara ngeceknya masih manual.
Petugas keliling, catat angka meteran di kertas, sebulan sekali.
Itu pun baru dilihat pas tagihan datang.
Jadi nggak ada data yang benar-benar sekarang.
Kalau ada yang boros, nggak ketahuan.
Kalau ada yang mulai bermasalah — kabel kepanasan, beban kelebihan — biasanya baru sadar setelah listrik ngadat.
Singkatnya: pengelola gedung kayak nyetir mobil tapi kaca depannya ketutup.

[Seg 3]
Nah, di sinilah SEMS masuk.
Alat ini dipasang di panel listrik, lalu "membaca" meteran-meteran yang sudah ada — sampai empat meteran sekaligus lewat satu kabel.
Datanya langsung dikirim ke server dan tampil di layar HP atau komputer, real-time.
Dan bukan cuma nampilin angka.
Ada kecerdasan buatan yang belajar pola pemakaian Anda, lalu bantu memperkirakan kebutuhan listrik ke depan.

[Seg 4]
Beberapa hal yang bikin SEMS praktis.
Satu alat bisa baca meteran dari merek yang beda-beda, di jalur yang sama.
Koneksinya dobel — pakai kabel LAN sebagai jalur utama, dan WiFi sebagai cadangan.
Kalau kabelnya lepas, otomatis pindah ke WiFi, datanya tetap jalan.
Ada juga empat sakelar pintar yang bisa dinyalain atau dimatiin dari jauh, lewat aplikasi.
Kalau arusnya kelebihan, alat ini langsung memutus sendiri supaya aman.
Mau setting? Cukup dari HP, nggak perlu bongkar alat.
Mau update software? Bisa dari jarak jauh.

[Seg 5]
Alat ini nggak jadi dalam semalam. Ini prosesnya.
Dimulai dari mendesain papan rangkaiannya — nentuin jalur dan tata letak komponen di komputer.
Papan yang sudah dicetak lalu diperiksa satu-satu sebelum dirakit.
Komponennya dipasang dan disolder — otak alatnya, modul jaringan, dan konektor-konektornya.
Semua bagian digabung jadi satu unit, lengkap sama kabel ke meteran dan ke sakelarnya.
Software-nya dimasukin ke alat, terus dicek jalan atau nggak.
Setting awal dilakukan lewat HP — sambungin ke jaringan, atur server, kenalin meterannya.
Begitu dinyalain, alat langsung baca meteran dan datanya muncul di layar.

[Seg 6]
Sebelum dipakai beneran, alat ini diuji habis-habisan.
Angkanya dicocokin sama alat ukur standar — hasilnya harus pas.
Empat meteran dibaca gantian di satu jalur, harus rapi, nggak boleh tabrakan.
Kabel LAN-nya sengaja dicabut — dan benar, alat langsung pindah ke WiFi tanpa kehilangan data.
Sakelar pengamannya diuji, memutus sendiri pas arus kelebihan.
Update software dari jauh juga dicoba sampai alat nyala lagi dengan versi baru.
Semuanya jalan sesuai rencana.

[Seg 7]
SEMS bisa dipakai di mana aja yang butuh listrik terpantau rapi — gedung kuliah, kantor pemerintahan, hotel, sampai rumah sakit.
Di semua tempat itu, tahu persis pemakaian listrik dan bisa ngatur beban dari jauh artinya biaya lebih hemat dan operasional lebih aman.

[Seg 8]
Tertarik pasang SEMS di gedung Anda?
Yuk, ngobrol.
Kontaknya ada di bawah ini.

[Seg 9]
Penelitian ini didanai oleh Direktorat Hilirisasi dan Kemitraan, Direktorat Jenderal Riset dan Pengembangan, Kementerian Pendidikan Tinggi, Sains, dan Teknologi.
Terima kasih juga untuk LPPM Universitas Sebelas Maret, dan semua yang terlibat.
```

---

### Rekap Estimasi Durasi

| Seg | Judul | Durasi | Kumulatif |
|-----|-------|--------|-----------|
| 0 | Bumper pembuka | 0:18 | 0:18 |
| 1 | Intro & latar belakang | 0:35 | 0:53 |
| 2 | Masalah | 0:34 | 1:27 |
| 3 | Solusi SEMS AIoT | 0:32 | 1:59 |
| 4 | Fitur & keunggulan | 0:39 | 2:38 |
| 5 | Proses pengembangan (PCB/assembly/rakit/uji coba) | 1:06 | 3:44 |
| 6 | Pengujian | 0:36 | 4:20 |
| 7 | Implementasi multi-sektor | 0:22 | 4:42 |
| 8 | Contact us | 0:08 | 4:50 |
| 9 | Penutup & credit | 0:10 | 5:00 |
| | **TOTAL** | **~5:00** | |

**Total kata VO ≈ 600** → pada 150 kata/menit murni baca ≈ 4:00; sisa ~1:00 untuk b-roll tanpa narasi, jeda, dan credit roll.

**Kompresi ke ~4:00** bila perlu: pangkas ruang b-roll Seg 5 (−20 dtk), rapatkan Seg 1–2 (−15 dtk), Seg 6 (−15 dtk). Jangan potong bumper (Seg 0) dan credit (Seg 9) — wajib.

---

## Catatan Produksi

- Semua adegan "bicara ke kamera" dihindari — pakai b-roll proses + VO + overlay teks (persis gaya contoh IPB).
- Musik: gunakan trek bebas royalti atau buatan sendiri; simpan bukti lisensi.
- Angka/klaim (persentase konsumsi energi, penghematan, level TKT) harus diisi dengan data valid dari proposal saat produksi — jangan mengarang.
- Siapkan storyboard (sketsa per-shot berurutan) mengikuti tabel Segmen 1–9 di atas.
- Contoh referensi praktik baik: video "Starter Kering Kultur Bakteri Bacillus aerophilus" DRI IPB — https://www.youtube.com/watch?v=Gvr2JyNAoAw
