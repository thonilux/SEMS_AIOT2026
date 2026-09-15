# Rancangan Alur Video — SEMS AIoT (Dortek 2026)

> Luaran video Program Hilirisasi Riset Prioritas, Skema Dorongan Teknologi 2026.
> Format dokumenter, durasi 3–5 menit, 1080p landscape, MP4/MOV/FLV.
> Voice over + subtitle/running text bahasa Indonesia sepanjang video.
> **Naskah VO versi final (revisi atasan)** — lihat [`docs/rev video.md`](rev%20video.md) untuk sumber asli.
> Pola alur mengikuti contoh praktik baik DRI IPB (Starter Kering Kultur Bakteri):
> latar belakang → masalah → solusi → **proses pengembangan langkah demi langkah** → pengujian → hasil & implementasi → penutup.

---

## 0. Bumper Pembuka (0:00–0:14) — WAJIB

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

## 1. Intro & Latar Belakang (0:14–0:46)

**Visual:** b-roll gedung kampus/perkantoran malam hari lampu menyala, panel listrik, meteran PLN berputar, ruang kerja full AC, koridor lampu menyala tanpa orang.

**Narasi (VO):**
> Setiap aktivitas di gedung modern mulai dari kampus, perkantoran, hotel, hingga rumah sakit sangat bergantung pada energi listrik. Namun, seiring meningkatnya kebutuhan operasional, konsumsi dan biaya listrik juga terus bertambah dari tahun ke tahun. Tantangannya, tidak semua pengelola gedung dapat mengetahui secara pasti bagaimana energi digunakan, di area mana terjadi pemborosan, dan perangkat apa yang paling banyak mengonsumsi listrik. Tanpa data yang akurat dan real-time, upaya efisiensi energi menjadi sulit dilakukan secara tepat. Di sinilah kebutuhan akan sistem manajemen energi yang cerdas menjadi semakin penting.

**Running text:** poin data konsumsi energi bangunan (isi angka valid saat produksi).

---

## 2. Masalah (0:46–1:01)

**Visual:** tagihan listrik menumpuk, orang mencatat meteran manual pakai kertas, panel listrik tanpa alat ukur, grafik beban naik, AC & lampu menyala di ruang kosong.

**Narasi (VO):**
> Di banyak gedung, pemantauan energi masih dilakukan secara manual dan berkala. Akibatnya, pemborosan, lonjakan beban, atau potensi gangguan sering terlambat diketahui. Tanpa data real-time, pengelola gedung baru mengetahui masalah setelah dampaknya terjadi.

**Running text:** 3 titik nyeri — *tidak real-time · tidak ada deteksi dini · tidak ada kendali jarak jauh*.

---

## 3. Solusi: SEMS AIoT (1:01–1:25)

**Visual:** reveal produk — koper demo SEMS dibuka, perangkat gateway ESP32, layar OLED menyala, dashboard live di laptop/HP, diagram arsitektur sederhana (meter → RS485 → gateway → MQTT → cloud/dashboard → AI).

**Narasi (VO):**
> Di sinilah SEMS hadir. Perangkat ini dipasang pada panel listrik untuk membaca data dari beberapa meter energi secara langsung. Data kemudian dikirim ke server dan ditampilkan secara real-time melalui komputer maupun perangkat mobile. Lebih dari sekadar monitoring, teknologi AI pada SEMS mempelajari pola konsumsi energi dan membantu memprediksi kebutuhan listrik untuk mendukung pengelolaan energi yang lebih efisien.

**Running text:** *Monitor real-time · Kendali jarak jauh · Proteksi otomatis · Prediksi berbasis AI*.

---

## 4. Fitur & Keunggulan (1:25–1:54)

**Visual:** screen recording web UI (Home, Network, MQTT, Modbus, Relay, System), close-up OLED navigasi 1 tombol, relay board 4 channel klik ON/OFF, notifikasi trip overcurrent, proses OTA update.

**Narasi (VO):**
> SEMS AIoT dirancang praktis dan fleksibel. Satu perangkat dapat membaca berbagai merek meter energi dalam satu jaringan, dengan koneksi utama melalui LAN dan WiFi sebagai cadangan agar data tetap terkirim saat terjadi gangguan koneksi. Sistem juga dilengkapi empat smart switch yang dapat dikendalikan dari jarak jauh, proteksi otomatis saat terjadi arus berlebih, serta konfigurasi dan pembaruan perangkat lunak yang dapat dilakukan langsung melalui aplikasi tanpa membuka perangkat.

**Running text (chip berurutan):** *Multi-meter 1 bus · LAN + WiFi failover · 4 Smart Switch + proteksi · Konfigurasi via app · OTA update*.

---

## 5. Proses Pelaksanaan & Pengembangan Produk (1:54–2:40) — INTI DOKUMENTER

> Segmen wajib: perancangan PCB, assembly, perakitan, uji coba. Ditampilkan **langkah demi langkah**, gaya proses seperti contoh IPB.

| # | Tahap | Visual b-roll |
|---|-------|---------------|
| 5.1 | Perancangan PCB & tata letak komponen | layar software EDA, penarikan jalur PCB, render 3D board |
| 5.2 | Pemeriksaan papan sirkuit | PCB hasil fabrikasi, inspeksi visual sebelum perakitan |
| 5.3 | Assembly komponen utama | penyolderan mikrokontroler, modul komunikasi, konektor — presisi |
| 5.4 | Integrasi & pengujian software | upload firmware, serial monitor, verifikasi fungsi |
| 5.5 | Konfigurasi awal via mobile | setting jaringan, server, meter energi lewat aplikasi/HP |
| 5.6 | Bring-up otomatis | perangkat aktif, langsung baca data & tampil real-time di sistem monitoring |

**Narasi (VO):**
> SEMS AIoT dikembangkan melalui proses engineering yang sistematis. Tahap awal dimulai dari perancangan PCB dan tata letak komponen, kemudian dilanjutkan dengan pemeriksaan papan sirkuit sebelum proses perakitan. Selanjutnya, seluruh komponen utama mulai dari mikrokontroler, modul komunikasi, hingga konektor diassembly secara presisi untuk memastikan kualitas dan keandalan perangkat. Setelah perangkat dirakit, software SEMS diintegrasikan dan diuji untuk memastikan seluruh fungsi berjalan dengan baik. Konfigurasi awal dapat dilakukan langsung melalui perangkat mobile, mulai dari pengaturan jaringan, server, hingga meter energi. Setelah aktif, SEMS secara otomatis membaca data dan menampilkannya secara real-time pada sistem monitoring.

**Musik:** instrumental proses (mirip segmen fermentasi contoh IPB), VO tetap ada di sepanjang segmen.

---

## 6. Pengujian (2:40–3:02)

> Padanan "pengujian rutin 6 minggu" pada contoh IPB.

**Visual:** setup uji di lab — sumber beban / meter referensi, tabel hasil pembacaan, grafik selisih pembacaan vs referensi, uji relay memutus beban, uji failover cabut kabel LAN, uji OTA, log MQTT broker menerima data terjadwal.

| Uji | Yang dibuktikan |
|-----|-----------------|
| Akurasi pembacaan meter | Selisih pembacaan gateway vs meter referensi dalam batas toleransi |
| Komunikasi multi-meter | Beberapa meter terbaca dalam satu jaringan tanpa tabrakan data |
| Perpindahan otomatis LAN ke WiFi | Kabel Ethernet dicabut → pindah ke WiFi otomatis, data tidak putus |
| Proteksi arus berlebih | Relai trip otomatis saat arus melewati ambang |
| Pembaruan software jarak jauh | Firmware baru diunggah via aplikasi, perangkat aktif dengan versi baru |

**Narasi (VO):**
> Sebelum digunakan, SEMS melalui serangkaian pengujian untuk memastikan akurasi, keandalan, dan keamanan sistem. Pengujian meliputi akurasi pembacaan meter, komunikasi multi-meter, perpindahan otomatis dari LAN ke WiFi, proteksi arus berlebih, hingga pembaruan software dari jarak jauh. Hasilnya, seluruh fungsi berjalan sesuai rancangan dan siap untuk tahap implementasi.

---

## 7. Implementasi di Berbagai Sektor (3:02–3:20)

**Visual:** montage 4 sektor — gedung universitas, kantor pemerintahan, hotel, rumah sakit — dengan overlay dashboard SEMS di tiap lokasi (boleh mockup/ilustrasi bila belum ada instalasi nyata; jangan klaim palsu).

**Narasi (VO):**
> SEMS dapat diterapkan pada berbagai fasilitas, mulai dari kampus, perkantoran, hotel, hingga rumah sakit. Dengan pemantauan energi secara real-time dan pengendalian beban dari jarak jauh, SEMS membantu meningkatkan efisiensi biaya sekaligus menjaga operasional sistem kelistrikan tetap aman dan andal.

**Running text:** capaian TKT yang ditargetkan (isi level sesuai proposal).

---

## 8. Contact Us (3:20–3:29)

**Visual:** kartu kontak — logo Nocola / Lab IoT FT UNS, alamat, email, website, QR.

**Narasi (VO):**
> Tertarik menerapkan SEMS di gedung Anda? Mari wujudkan pengelolaan energi yang lebih cerdas, efisien, dan terintegrasi. Segera hubungi kami.

---

## 9. Penutup: Ucapan Terima Kasih & Credit (3:29–3:48) — WAJIB

**Visual:** credit roll.

**Narasi (VO):**
> Penelitian ini didanai oleh Direktorat Hilirisasi dan Kemitraan, Direktorat Jenderal Riset dan Pengembangan, Kementerian Pendidikan Tinggi, Sains, dan Teknologi. Terima kasih juga untuk LPPM Universitas Sebelas Maret, dan semua yang terlibat.

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

## Naskah Voice Over Final (Revisi Atasan) + Estimasi Durasi

> Gaya: formal-profesional, kalimat lengkap, nada institusional-meyakinkan (bukan santai/ngobrol).
> Sumber: [`docs/rev video.md`](rev%20video.md) — jangan ubah substansi kalimat tanpa approval ulang.
> Tempo baca acuan: **140 kata/menit** (formal, tidak buru-buru, lebih pelan dari gaya santai).

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
| `[pelan]` ... `[/pelan]` | perlambat tempo | — | nama lembaga, nomor kontrak, angka |

**Cara pakai per engine:**

- **ElevenLabs** — ganti `[jeda-pendek]` menjadi `<break time="0.4s" />`, `[jeda]` menjadi `<break time="0.7s" />`, `[jeda-panjang]` menjadi `<break time="1.2s" />`. `[napas]` menjadi `<break time="0.5s" />`. Hapus tag `[tekan:]`/`[pelan]` — atur lewat slider Stability/Style.
- **OpenAI TTS / gpt-4o-mini-tts** — hapus semua tag, ganti `[jeda]` dengan tanda titik + baris baru; kendalikan tempo lewat instruksi prompt ("baca formal, tenang, jeda jelas antar kalimat").
- **Google Cloud TTS (SSML)** — `[jeda-pendek]` → `<break time="300ms"/>`, `[jeda]` → `<break time="700ms"/>`, `[jeda-panjang]` → `<break time="1200ms"/>`. `[pelan]...[/pelan]` → `<prosody rate="90%">...</prosody>`. `[tekan: X]` → `<emphasis level="strong">X</emphasis>`. Bungkus tiap segmen dengan `<speak>...</speak>`.
- **Azure Speech (SSML)** — sama seperti Google; `[pelan]` → `<prosody rate="-10%">`.
- **Coqui / Piper / TTS lokal** — hapus semua tag, pecah jadi 1 kalimat per baris; jeda dihasilkan dari tanda baca.

**Prompt gaya (tempel di system/instruction TTS):**
> "Baca sebagai narator dokumenter Indonesia yang formal, jelas, dan meyakinkan — gaya video korporat/institusi riset. Tempo sedang-pelan, tidak terburu-buru. Beri jeda jelas antar kalimat. Artikulasi jelas, terutama untuk nama lembaga dan nomor kontrak."

**Voice:** pilih 1 voice Indonesia natural, nada dewasa berwibawa (pria atau wanita), konsisten untuk seluruh 10 segmen. Rekam per segmen (file terpisah `vo_seg0.wav` … `vo_seg9.wav`) supaya gampang re-take.

---

### Seg 0 — Bumper Pembuka
**VO (dengan penanda jeda):**
> [tekan: Perkenalkan] Produk Smart Energy Management System Berbasis Artificial Intelligence and Internet of Things — [jeda-pendek] SEMS-AIoT. [jeda]
> Solusi cerdas untuk memantau dan mengelola penggunaan energi listrik. [jeda-panjang]
> [pelan] Hasil penelitian Program Hilirisasi Riset Prioritas, [jeda-pendek] Skema Dorongan Teknologi Tahun dua ribu dua puluh enam. [/pelan]

Kata: ~30 · Baca (140 kwpm): ~13 dtk · **Segmen: 0:00–0:14 (14 dtk)**.

### Seg 1 — Intro & Latar Belakang
**VO (dengan penanda jeda):**
> Setiap aktivitas di gedung modern — [jeda-pendek] mulai dari kampus, [jeda-pendek] perkantoran, [jeda-pendek] hotel, [jeda-pendek] hingga rumah sakit — [jeda-pendek] sangat bergantung pada energi listrik. [jeda-panjang]
> Namun, [jeda-pendek] seiring meningkatnya kebutuhan operasional, [jeda-pendek] konsumsi dan biaya listrik juga terus bertambah dari tahun ke tahun. [jeda]
> [napas] Tantangannya, [jeda-pendek] tidak semua pengelola gedung dapat mengetahui secara pasti bagaimana energi digunakan, [jeda-pendek] di area mana terjadi pemborosan, [jeda-pendek] dan perangkat apa yang paling banyak mengonsumsi listrik. [jeda-panjang]
> Tanpa data yang akurat dan real-time, [jeda-pendek] upaya efisiensi energi menjadi sulit dilakukan secara tepat. [jeda]
> Di sinilah kebutuhan akan sistem manajemen energi yang cerdas menjadi semakin [tekan: penting].

Kata: ~75 · Baca (140 kwpm): ~32 dtk · **Segmen: 0:14–0:46 (32 dtk)**.

### Seg 2 — Masalah
**VO (dengan penanda jeda):**
> Di banyak gedung, [jeda-pendek] pemantauan energi masih dilakukan secara manual dan berkala. [jeda-panjang]
> Akibatnya, [jeda-pendek] pemborosan, [jeda-pendek] lonjakan beban, [jeda-pendek] atau potensi gangguan [jeda-pendek] sering terlambat diketahui. [jeda]
> Tanpa data real-time, [jeda-pendek] pengelola gedung baru mengetahui masalah [tekan: setelah dampaknya terjadi].

Kata: ~36 · Baca (140 kwpm): ~15 dtk · **Segmen: 0:46–1:01 (15 dtk)**.

### Seg 3 — Solusi: SEMS AIoT
**VO (dengan penanda jeda):**
> Di sinilah SEMS hadir. [jeda-panjang]
> Perangkat ini dipasang pada panel listrik [jeda-pendek] untuk membaca data dari beberapa meter energi secara langsung. [jeda]
> Data kemudian dikirim ke server [jeda-pendek] dan ditampilkan secara real-time [jeda-pendek] melalui komputer maupun perangkat mobile. [jeda-panjang]
> [napas] Lebih dari sekadar monitoring, [jeda-pendek] teknologi AI pada SEMS mempelajari pola konsumsi energi [jeda-pendek] dan membantu memprediksi kebutuhan listrik [jeda-pendek] untuk mendukung pengelolaan energi yang lebih [tekan: efisien].

Kata: ~55 · Baca (140 kwpm): ~24 dtk · **Segmen: 1:01–1:25 (24 dtk)**.

### Seg 4 — Fitur & Keunggulan
**VO (dengan penanda jeda):**
> SEMS AIoT dirancang praktis dan fleksibel. [jeda-panjang]
> Satu perangkat dapat membaca berbagai merek meter energi dalam satu jaringan, [jeda-pendek] dengan koneksi utama melalui LAN [jeda-pendek] dan WiFi sebagai cadangan [jeda-pendek] agar data tetap terkirim saat terjadi gangguan koneksi. [jeda-panjang]
> [napas] Sistem juga dilengkapi empat smart switch yang dapat dikendalikan dari jarak jauh, [jeda-pendek] proteksi otomatis saat terjadi arus berlebih, [jeda-pendek] serta konfigurasi dan pembaruan perangkat lunak [jeda-pendek] yang dapat dilakukan langsung melalui aplikasi [jeda-pendek] tanpa membuka perangkat.

Kata: ~68 · Baca (140 kwpm): ~29 dtk · **Segmen: 1:25–1:54 (29 dtk)**.

### Seg 5 — Proses Pelaksanaan & Pengembangan (inti dokumenter)
**VO (per langkah, ada jeda musik proses di antaranya):**
> SEMS AIoT dikembangkan melalui proses engineering yang sistematis. [jeda-panjang]
> Tahap awal dimulai dari perancangan PCB dan tata letak komponen, [jeda-pendek] kemudian dilanjutkan dengan pemeriksaan papan sirkuit sebelum proses perakitan. [jeda-panjang]
> Selanjutnya, [jeda-pendek] seluruh komponen utama — [jeda-pendek] mulai dari mikrokontroler, [jeda-pendek] modul komunikasi, [jeda-pendek] hingga konektor — [jeda-pendek] diassembly secara presisi [jeda-pendek] untuk memastikan kualitas dan keandalan perangkat. [jeda-panjang]
> [napas] Setelah perangkat dirakit, [jeda-pendek] software SEMS diintegrasikan dan diuji [jeda-pendek] untuk memastikan seluruh fungsi berjalan dengan baik. [jeda]
> Konfigurasi awal dapat dilakukan langsung melalui perangkat mobile, [jeda-pendek] mulai dari pengaturan jaringan, [jeda-pendek] server, [jeda-pendek] hingga meter energi. [jeda-panjang]
> Setelah aktif, [jeda-pendek] SEMS secara otomatis membaca data [jeda-pendek] dan menampilkannya secara real-time pada sistem monitoring.

Kata: ~107 · Baca (140 kwpm): ~46 dtk · **Segmen: 1:54–2:40 (46 dtk)**.

### Seg 6 — Pengujian
**VO (dengan penanda jeda):**
> Sebelum digunakan, [jeda-pendek] SEMS melalui serangkaian pengujian [jeda-pendek] untuk memastikan akurasi, [jeda-pendek] keandalan, [jeda-pendek] dan keamanan sistem. [jeda-panjang]
> Pengujian meliputi akurasi pembacaan meter, [jeda-pendek] komunikasi multi-meter, [jeda-pendek] perpindahan otomatis dari LAN ke WiFi, [jeda-pendek] proteksi arus berlebih, [jeda-pendek] hingga pembaruan software dari jarak jauh. [jeda-panjang]
> Hasilnya, [jeda-pendek] seluruh fungsi berjalan sesuai rancangan [jeda-pendek] dan siap untuk tahap [tekan: implementasi].

Kata: ~51 · Baca (140 kwpm): ~22 dtk · **Segmen: 2:40–3:02 (22 dtk)**.

### Seg 7 — Implementasi di Berbagai Sektor
**VO (dengan penanda jeda):**
> SEMS dapat diterapkan pada berbagai fasilitas, [jeda-pendek] mulai dari kampus, [jeda-pendek] perkantoran, [jeda-pendek] hotel, [jeda-pendek] hingga rumah sakit. [jeda-panjang]
> [napas] Dengan pemantauan energi secara real-time [jeda-pendek] dan pengendalian beban dari jarak jauh, [jeda-pendek] SEMS membantu meningkatkan efisiensi biaya [jeda-pendek] sekaligus menjaga operasional sistem kelistrikan tetap aman dan andal.

Kata: ~41 · Baca (140 kwpm): ~18 dtk · **Segmen: 3:02–3:20 (18 dtk)**.

### Seg 8 — Contact Us
**VO (dengan penanda jeda):**
> Tertarik menerapkan SEMS di gedung Anda? [jeda]
> Mari wujudkan pengelolaan energi yang lebih cerdas, [jeda-pendek] efisien, [jeda-pendek] dan terintegrasi. [jeda-panjang]
> Segera hubungi kami.

Kata: ~21 · Baca (140 kwpm): ~9 dtk · **Segmen: 3:20–3:29 (9 dtk)**.

### Seg 9 — Penutup & Credit
**VO (dengan penanda jeda — dibaca formal, artikulasi jelas):**
> [pelan] Penelitian ini didanai oleh [jeda-pendek] Direktorat Hilirisasi dan Kemitraan, [jeda-pendek] Direktorat Jenderal Riset dan Pengembangan, [jeda-pendek] Kementerian Pendidikan Tinggi, Sains, dan Teknologi. [/pelan] [jeda-panjang]
> Terima kasih juga untuk LPPM Universitas Sebelas Maret, [jeda-pendek] dan semua yang terlibat.

Kata: ~33 · Baca (140 kwpm): ~14 dtk · + credit roll senyap ~5 dtk · **Segmen: 3:29–3:48 (19 dtk)**.

---

### Naskah Bersih (tanpa tag, cadangan)

Kalau engine TTS-nya menolak tag, pakai versi ini — 1 kalimat per baris, jeda dari tanda baca:

```
[Seg 0]
Perkenalkan Produk Smart Energy Management System Berbasis Artificial Intelligence and Internet of Things (SEMS-AIoT).
Solusi cerdas untuk memantau dan mengelola penggunaan energi listrik.
Hasil penelitian Program Hilirisasi Riset Prioritas - Skema Dorongan Teknologi Tahun 2026.

[Seg 1]
Setiap aktivitas di gedung modern mulai dari kampus, perkantoran, hotel, hingga rumah sakit sangat bergantung pada energi listrik.
Namun, seiring meningkatnya kebutuhan operasional, konsumsi dan biaya listrik juga terus bertambah dari tahun ke tahun.
Tantangannya, tidak semua pengelola gedung dapat mengetahui secara pasti bagaimana energi digunakan, di area mana terjadi pemborosan, dan perangkat apa yang paling banyak mengonsumsi listrik.
Tanpa data yang akurat dan real-time, upaya efisiensi energi menjadi sulit dilakukan secara tepat. Di sinilah kebutuhan akan sistem manajemen energi yang cerdas menjadi semakin penting.

[Seg 2]
Di banyak gedung, pemantauan energi masih dilakukan secara manual dan berkala. Akibatnya, pemborosan, lonjakan beban, atau potensi gangguan sering terlambat diketahui.
Tanpa data real-time, pengelola gedung baru mengetahui masalah setelah dampaknya terjadi.

[Seg 3]
Di sinilah SEMS hadir. Perangkat ini dipasang pada panel listrik untuk membaca data dari beberapa meter energi secara langsung.
Data kemudian dikirim ke server dan ditampilkan secara real-time melalui komputer maupun perangkat mobile. Lebih dari sekadar monitoring, teknologi AI pada SEMS mempelajari pola konsumsi energi dan membantu memprediksi kebutuhan listrik untuk mendukung pengelolaan energi yang lebih efisien.

[Seg 4]
SEMS AIoT dirancang praktis dan fleksibel. Satu perangkat dapat membaca berbagai merek meter energi dalam satu jaringan, dengan koneksi utama melalui LAN dan WiFi sebagai cadangan agar data tetap terkirim saat terjadi gangguan koneksi.
Sistem juga dilengkapi empat smart switch yang dapat dikendalikan dari jarak jauh, proteksi otomatis saat terjadi arus berlebih, serta konfigurasi dan pembaruan perangkat lunak yang dapat dilakukan langsung melalui aplikasi tanpa membuka perangkat.

[Seg 5]
SEMS AIoT dikembangkan melalui proses engineering yang sistematis. Tahap awal dimulai dari perancangan PCB dan tata letak komponen, kemudian dilanjutkan dengan pemeriksaan papan sirkuit sebelum proses perakitan. Selanjutnya, seluruh komponen utama mulai dari mikrokontroler, modul komunikasi, hingga konektor diassembly secara presisi untuk memastikan kualitas dan keandalan perangkat.
Setelah perangkat dirakit, software SEMS diintegrasikan dan diuji untuk memastikan seluruh fungsi berjalan dengan baik. Konfigurasi awal dapat dilakukan langsung melalui perangkat mobile, mulai dari pengaturan jaringan, server, hingga meter energi. Setelah aktif, SEMS secara otomatis membaca data dan menampilkannya secara real-time pada sistem monitoring.

[Seg 6]
Sebelum digunakan, SEMS melalui serangkaian pengujian untuk memastikan akurasi, keandalan, dan keamanan sistem.
Pengujian meliputi akurasi pembacaan meter, komunikasi multi-meter, perpindahan otomatis dari LAN ke WiFi, proteksi arus berlebih, hingga pembaruan software dari jarak jauh. Hasilnya, seluruh fungsi berjalan sesuai rancangan dan siap untuk tahap implementasi.

[Seg 7]
SEMS dapat diterapkan pada berbagai fasilitas, mulai dari kampus, perkantoran, hotel, hingga rumah sakit.
Dengan pemantauan energi secara real-time dan pengendalian beban dari jarak jauh, SEMS membantu meningkatkan efisiensi biaya sekaligus menjaga operasional sistem kelistrikan tetap aman dan andal.

[Seg 8]
Tertarik menerapkan SEMS di gedung Anda?
Mari wujudkan pengelolaan energi yang lebih cerdas, efisien, dan terintegrasi. Segera hubungi kami.

[Seg 9]
Penelitian ini didanai oleh Direktorat Hilirisasi dan Kemitraan, Direktorat Jenderal Riset dan Pengembangan, Kementerian Pendidikan Tinggi, Sains, dan Teknologi.
Terima kasih juga untuk LPPM Universitas Sebelas Maret, dan semua yang terlibat.
```

---

### Format Siap-Pakai — ElevenLabs TTS (SSML `<break>`)

Tempel langsung per segmen ke ElevenLabs (Text-to-Speech / Studio). Mapping: `[jeda-pendek]`→0.4s, `[jeda]`→0.7s, `[jeda-panjang]`→1.2s, `[napas]`→0.5s. Tag `[tekan:]`/`[pelan]` dihapus (ElevenLabs belum stabil di emphasis/prosody manual) — ganti penekanan lewat setting Stability/Style atau intonasi natural dari kalimat sekitarnya.

**Setting disarankan:** Model `eleven_multilingual_v2` (atau v3 kalau tersedia) · Stability 45–55% (lebih tinggi dari gaya santai, biar stabil-formal) · Similarity 75–85% · Style 0–5% · Speaker Boost ON.

```
[Seg 0 — Bumper Pembuka]
Perkenalkan Produk Smart Energy Management System Berbasis Artificial Intelligence and Internet of Things — SEMS-AIoT. <break time="0.7s" />
Solusi cerdas untuk memantau dan mengelola penggunaan energi listrik. <break time="1.2s" />
Hasil penelitian Program Hilirisasi Riset Prioritas, <break time="0.4s" /> Skema Dorongan Teknologi Tahun dua ribu dua puluh enam.

[Seg 1 — Intro & Latar Belakang]
Setiap aktivitas di gedung modern — <break time="0.4s" /> mulai dari kampus, <break time="0.4s" /> perkantoran, <break time="0.4s" /> hotel, <break time="0.4s" /> hingga rumah sakit — <break time="0.4s" /> sangat bergantung pada energi listrik. <break time="1.2s" />
Namun, <break time="0.4s" /> seiring meningkatnya kebutuhan operasional, <break time="0.4s" /> konsumsi dan biaya listrik juga terus bertambah dari tahun ke tahun. <break time="0.7s" />
Tantangannya, <break time="0.4s" /> tidak semua pengelola gedung dapat mengetahui secara pasti bagaimana energi digunakan, <break time="0.4s" /> di area mana terjadi pemborosan, <break time="0.4s" /> dan perangkat apa yang paling banyak mengonsumsi listrik. <break time="1.2s" />
Tanpa data yang akurat dan real-time, <break time="0.4s" /> upaya efisiensi energi menjadi sulit dilakukan secara tepat. <break time="0.7s" />
Di sinilah kebutuhan akan sistem manajemen energi yang cerdas menjadi semakin penting.

[Seg 2 — Masalah]
Di banyak gedung, <break time="0.4s" /> pemantauan energi masih dilakukan secara manual dan berkala. <break time="1.2s" />
Akibatnya, <break time="0.4s" /> pemborosan, <break time="0.4s" /> lonjakan beban, <break time="0.4s" /> atau potensi gangguan <break time="0.4s" /> sering terlambat diketahui. <break time="0.7s" />
Tanpa data real-time, <break time="0.4s" /> pengelola gedung baru mengetahui masalah setelah dampaknya terjadi.

[Seg 3 — Solusi: SEMS AIoT]
Di sinilah SEMS hadir. <break time="1.2s" />
Perangkat ini dipasang pada panel listrik <break time="0.4s" /> untuk membaca data dari beberapa meter energi secara langsung. <break time="0.7s" />
Data kemudian dikirim ke server <break time="0.4s" /> dan ditampilkan secara real-time <break time="0.4s" /> melalui komputer maupun perangkat mobile. <break time="1.2s" />
Lebih dari sekadar monitoring, <break time="0.4s" /> teknologi AI pada SEMS mempelajari pola konsumsi energi <break time="0.4s" /> dan membantu memprediksi kebutuhan listrik <break time="0.4s" /> untuk mendukung pengelolaan energi yang lebih efisien.

[Seg 4 — Fitur & Keunggulan]
SEMS AIoT dirancang praktis dan fleksibel. <break time="1.2s" />
Satu perangkat dapat membaca berbagai merek meter energi dalam satu jaringan, <break time="0.4s" /> dengan koneksi utama melalui LAN <break time="0.4s" /> dan WiFi sebagai cadangan <break time="0.4s" /> agar data tetap terkirim saat terjadi gangguan koneksi. <break time="1.2s" />
Sistem juga dilengkapi empat smart switch yang dapat dikendalikan dari jarak jauh, <break time="0.4s" /> proteksi otomatis saat terjadi arus berlebih, <break time="0.4s" /> serta konfigurasi dan pembaruan perangkat lunak <break time="0.4s" /> yang dapat dilakukan langsung melalui aplikasi <break time="0.4s" /> tanpa membuka perangkat.

[Seg 5 — Proses Pelaksanaan & Pengembangan]
SEMS AIoT dikembangkan melalui proses engineering yang sistematis. <break time="1.2s" />
Tahap awal dimulai dari perancangan PCB dan tata letak komponen, <break time="0.4s" /> kemudian dilanjutkan dengan pemeriksaan papan sirkuit sebelum proses perakitan. <break time="1.2s" />
Selanjutnya, <break time="0.4s" /> seluruh komponen utama — <break time="0.4s" /> mulai dari mikrokontroler, <break time="0.4s" /> modul komunikasi, <break time="0.4s" /> hingga konektor — <break time="0.4s" /> diassembly secara presisi <break time="0.4s" /> untuk memastikan kualitas dan keandalan perangkat. <break time="1.2s" />
Setelah perangkat dirakit, <break time="0.4s" /> software SEMS diintegrasikan dan diuji <break time="0.4s" /> untuk memastikan seluruh fungsi berjalan dengan baik. <break time="0.7s" />
Konfigurasi awal dapat dilakukan langsung melalui perangkat mobile, <break time="0.4s" /> mulai dari pengaturan jaringan, <break time="0.4s" /> server, <break time="0.4s" /> hingga meter energi. <break time="1.2s" />
Setelah aktif, <break time="0.4s" /> SEMS secara otomatis membaca data <break time="0.4s" /> dan menampilkannya secara real-time pada sistem monitoring.

[Seg 6 — Pengujian]
Sebelum digunakan, <break time="0.4s" /> SEMS melalui serangkaian pengujian <break time="0.4s" /> untuk memastikan akurasi, <break time="0.4s" /> keandalan, <break time="0.4s" /> dan keamanan sistem. <break time="1.2s" />
Pengujian meliputi akurasi pembacaan meter, <break time="0.4s" /> komunikasi multi-meter, <break time="0.4s" /> perpindahan otomatis dari LAN ke WiFi, <break time="0.4s" /> proteksi arus berlebih, <break time="0.4s" /> hingga pembaruan software dari jarak jauh. <break time="1.2s" />
Hasilnya, <break time="0.4s" /> seluruh fungsi berjalan sesuai rancangan <break time="0.4s" /> dan siap untuk tahap implementasi.

[Seg 7 — Implementasi di Berbagai Sektor]
SEMS dapat diterapkan pada berbagai fasilitas, <break time="0.4s" /> mulai dari kampus, <break time="0.4s" /> perkantoran, <break time="0.4s" /> hotel, <break time="0.4s" /> hingga rumah sakit. <break time="1.2s" />
Dengan pemantauan energi secara real-time <break time="0.4s" /> dan pengendalian beban dari jarak jauh, <break time="0.4s" /> SEMS membantu meningkatkan efisiensi biaya <break time="0.4s" /> sekaligus menjaga operasional sistem kelistrikan tetap aman dan andal.

[Seg 8 — Contact Us]
Tertarik menerapkan SEMS di gedung Anda? <break time="0.7s" />
Mari wujudkan pengelolaan energi yang lebih cerdas, <break time="0.4s" /> efisien, <break time="0.4s" /> dan terintegrasi. <break time="1.2s" />
Segera hubungi kami.

[Seg 9 — Penutup & Credit]
Penelitian ini didanai oleh <break time="0.4s" /> Direktorat Hilirisasi dan Kemitraan, <break time="0.4s" /> Direktorat Jenderal Riset dan Pengembangan, <break time="0.4s" /> Kementerian Pendidikan Tinggi, Sains, dan Teknologi. <break time="1.2s" />
Terima kasih juga untuk LPPM Universitas Sebelas Maret, <break time="0.4s" /> dan semua yang terlibat.
```

> Catatan ElevenLabs:
> - Kalau versi/plan tidak mendukung tag `<break>` di Text-to-Speech biasa, pakai fitur **Studio / Projects** (mendukung SSML penuh) — atau turunkan ke [naskah bersih](#naskah-bersih-tanpa-tag-cadangan) tanpa tag dan atur jeda lewat titik/koma + regenerate per kalimat.
> - Render tiap segmen sebagai file terpisah (`vo_seg0.mp3` … `vo_seg9.mp3`) supaya gampang re-take tanpa render ulang semua.
> - `<break>` beruntun (>3 detik total) kadang di-skip oleh model — kalau jeda kepanjangan disingkat sendiri saat generate, pecah jadi generate per-kalimat lalu gabung manual di editor audio.
> - Nama lembaga panjang (Seg 0, Seg 9) dan istilah asing (AIoT, real-time) kadang salah lafal — cek hasil generate, re-take kalau perlu, atau eja fonetis manual di prompt kalau engine mendukung.

---

### Rekap Estimasi Durasi

| Seg | Judul | Durasi | Kumulatif |
|-----|-------|--------|-----------|
| 0 | Bumper pembuka | 0:14 | 0:14 |
| 1 | Intro & latar belakang | 0:32 | 0:46 |
| 2 | Masalah | 0:15 | 1:01 |
| 3 | Solusi SEMS AIoT | 0:24 | 1:25 |
| 4 | Fitur & keunggulan | 0:29 | 1:54 |
| 5 | Proses pengembangan (PCB/assembly/rakit/uji coba) | 0:46 | 2:40 |
| 6 | Pengujian | 0:22 | 3:02 |
| 7 | Implementasi multi-sektor | 0:18 | 3:20 |
| 8 | Contact us | 0:09 | 3:29 |
| 9 | Penutup & credit | 0:19 | 3:48 |
| | **TOTAL** | **~3:48** | |

**Total kata VO ≈ 517** → pada 140 kata/menit murni baca ≈ 3:41; sisa ~7 dtk untuk credit roll senyap.

**Video di bawah minimum 5 menit yang biasa dipakai contoh (durasi wajib 3–5 menit, jadi 3:48 masih valid).** Kalau mau mendekati 5 menit untuk ruang b-roll lebih lega, perpanjang jeda antar-shot di Segmen 5 (proses) dan Segmen 7 (implementasi multi-sektor) tanpa mengubah naskah VO — cukup tambah durasi visual/musik instrumental tanpa narasi di sela-sela segmen tersebut, sampai total 4:30–5:00.

---

## Catatan Produksi

- Semua adegan "bicara ke kamera" dihindari — pakai b-roll proses + VO + overlay teks (persis gaya contoh IPB).
- Musik: gunakan trek bebas royalti atau buatan sendiri; simpan bukti lisensi.
- Angka/klaim (persentase konsumsi energi, penghematan, level TKT) harus diisi dengan data valid dari proposal saat produksi — jangan mengarang.
- Siapkan storyboard (sketsa per-shot berurutan) mengikuti tabel Segmen 1–9 di atas.
- Naskah VO final di atas adalah **revisi resmi dari atasan** ([`docs/rev video.md`](rev%20video.md)) — perubahan lanjutan harus lewat approval ulang, jangan diedit sepihak.
- Contoh referensi praktik baik: video "Starter Kering Kultur Bakteri Bacillus aerophilus" DRI IPB — https://www.youtube.com/watch?v=Gvr2JyNAoAw
