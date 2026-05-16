# SmartLab Asistan — CLAUDE.md
> ESP32-S3 tabanlı AI destekli laboratuvar asistanı. Bu dosya projeye tam hakimiyet için yazılmıştır.

---

## Proje Özeti

**SmartLab Asistan** — ESP32-S3 üzerinde çalışan, sesli komutla etkileşime giren, RFID ile kullanıcı tanıyan, duman algılayan ve fan kontrol eden IoT final projesi.

- **Donanım:** ESP32-S3 N16R8 (16MB Flash, 8MB Octal PSRAM)
- **Framework:** ESP-IDF 5.5.3, C dili, VS Code
- **Proje klasörü:** `iot-project-final-v1/`
- **Öğrenci:** Abdulsamet Kara

---

## Donanım Listesi ve PIN Tablosu

| Bileşen | Arayüz | Pinler |
|---|---|---|
| ILI9341 TFT (240×320) | SPI | MOSI=11, MISO=13, CLK=12, CS=10, DC=9, RST=8, BL=46 |
| MFRC522 RFID | SPI (aynı bus) | CS=7, RST=6 |
| INMP441 Mikrofon | I2S | WS=42, SCK=41, SD=40 |
| MAX98357A Hoparlör | I2S | WS=39, BCK=38, DIN=37 |
| MQ135 Duman Sensörü | ADC | AOUT=GPIO3 (ADC1_CH2) |
| L298N Fan Sürücü | LEDC PWM | IN4=GPIO21, ENA=GPIO18 |
| PTT Butonu | GPIO | GPIO0 (BOOT butonu) |

> **Önemli:** GPIO7, ILI9341 RST'ye ayrılmıştır. Fan IN4 bu yüzden GPIO21'dedir (vizeden farklı).

---

## Sunucu (PC tarafı)

- **Dil:** Python, FastAPI
- **WebSocket endpoint:** `ws://<PC_IP>:8080/ws`
- **STT:** Faster-Whisper (`base` model, GPU, `language="tr"`)
- **TTS:** Piper (`tr_TR-dfki-medium.onnx`, raw PCM çıkışı, 22050Hz 16-bit mono)
- **Klasör:** `iot-project-final-v1/server/`

---

## Dosya Yapısı

```
iot-project-final-v1/
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── config.h              ← WiFi/IP/Pin tanımları BURAYA
│   ├── main.c                ← Ana akış, FreeRTOS görevleri
│   ├── crypto.c / crypto.h   ← AES-256-CBC (mbedTLS hardware)
│   ├── ws_client.c / ws_client.h  ← WebSocket istemci
│   ├── rfid.c / rfid.h       ← MFRC522 bare-metal SPI driver
│   ├── i2s_mic.c / i2s_mic.h ← INMP441 mikrofon (vizeden)
│   ├── i2s_player.c / i2s_player.h ← MAX98357A hoparlör (vizeden)
│   └── ui/
│       ├── display.c / display.h   ← LVGL + ILI9341 init
│       └── ui_smartlab.c / ui_smartlab.h ← TFT state machine
├── server/
│   ├── main.py               ← FastAPI WebSocket sunucusu
│   ├── session.py            ← SessionStore, RFID_USERS
│   ├── crypto.py             ← Python AES-256-CBC
│   ├── stt.py                ← Faster-Whisper STT
│   └── tts.py                ← Piper TTS
├── sdkconfig.defaults        ← Build konfigürasyonu
└── smartlab_proje_taslağı.md ← Proje taslağı (v1.1)
```

---

## config.h — Kullanıcının Düzenlemesi Gereken Satırlar

```c
// Bu 3 satırı kendi bilgilerine göre değiştir:
#define WIFI_SSID     "YOUR_WIFI_SSID"      // WiFi ağ adı
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"   // WiFi şifresi
#define SERVER_HOST   "YOUR_SERVER_IP"       // PC'nin yerel IP'si (ipconfig ile bak)
```

---

## İletişim Protokolü (WebSocket Binary + JSON Karışık)

### Bağlantı kurulunca (Server → ESP32, JSON):
```json
{"type": "session", "session_id": "...", "key_b64": "<base64 AES-256 key>"}
```

### RFID okununca (ESP32 → Server, JSON):
```json
{"type": "rfid", "uid": "A1B2C3D4", "session_id": "..."}
```

### Server kullanıcı adı dönüşü (Server → ESP32, JSON):
```json
{"type": "user", "name": "Abdulsamet Kara"}
```

### Ses gönderimi (ESP32 → Server, Binary frame):
```
[0x01][IV(16 byte)][AES-256-CBC şifreli PCM]
```

### Ses yanıtı (Server → ESP32, Binary frame):
```
[IV(16 byte)][AES-256-CBC şifreli PCM]
```

> **Fragmentation:** Büyük ses yanıtları (200-400KB) birden fazla WebSocket frame'de gelir.
> `ws_client.c` içinde PSRAM'da `s_accum` buffer'ı ile toplanır, `fin` flag gelince işlenir.

---

## AES-256-CBC Detayları

- **Anahtar:** 32 byte, her oturumda server rastgele üretir, base64 JSON ile gönderir
- **IV:** Her mesajda 32 byte, `esp_fill_random` ile üretilir, frame başına eklenir
- **Padding:** PKCS#7, PCM verisi şifrelemeden önce `crypto_pad_pcm()` ile pad edilir
- **Hardware hızlandırma:** `CONFIG_MBEDTLS_HARDWARE_AES=y` (ESP32-S3 yerleşik AES)

---

## TFT State Machine (ui_smartlab.c)

| State | Ekran | Açıklama |
|---|---|---|
| `UI_IDLE` | Gri, "SmartLab Asistan" | Başlangıç, WiFi/WS bekliyor |
| `UI_RFID_READ` | Mavi, UID göster | Kart okundu, kullanıcı bekleniyor |
| `UI_READY` | Yeşil, kullanıcı adı | Oturum açıldı, PTT bekliyor |
| `UI_RECORDING` | Kırmızı, "Dinliyorum..." | PTT basılı, ses kaydediliyor |
| `UI_PROCESSING` | Sarı, spinner | Sunucuya gönderildi, yanıt bekleniyor |
| `UI_SPEAKING` | Cyan, "Konuşuyor..." | TTS çalıyor |
| `UI_SMOKE_ALERT` | Kırmızı BG, sarı ikon | "DUMAN TESPİT EDİLDİ!" |
| `UI_ERROR` | Koyu kırmızı | Hata mesajı |

---

## FAZA DURUMU

### ✅ FAZA 1 — Temel İletişim (TAMAMLANDI, build bekleniyor)

**Kapsam:** WebSocket bağlantısı + AES-256 şifreleme + TFT state machine

**Yapılanlar:**
- `main.c` — WiFi init, SPI bus, tüm modüllerin init sırası, ana RFID+PTT döngüsü
- `crypto.c/h` — AES-256-CBC encrypt/decrypt, hardware RNG, PKCS#7 padding
- `ws_client.c/h` — WebSocket client, session key exchange, binary frame fragmentation
- `rfid.c/h` — MFRC522 bare-metal SPI (REQA → AntiColl → UID → HLTA)
- `ui/display.c/h` — LVGL 8.3 init, ILI9341 flush, Core 1'e pin
- `ui/ui_smartlab.c/h` — 8 durumlu TFT state machine
- `server/main.py` — FastAPI WebSocket, session yönetimi, STT/TTS pipeline
- `server/session.py` — SessionStore + RFID_USERS dict
- `server/crypto.py` — Python AES-256-CBC
- `server/stt.py` — Faster-Whisper (GPU, Turkish)
- `server/tts.py` — Piper TTS (raw PCM)
- `sdkconfig.defaults` — ESP32-S3, 16MB flash, Octal PSRAM, mbedTLS HW AES

**Bekleyen build sorunu:**
- `sdkconfig.defaults`'a `CONFIG_COMPILER_OPTIMIZATION_NONE=y` EKLENDİ (GCC 14.2.0 ICE fix)
- Kullanıcının `build/` klasörünü ve `sdkconfig` dosyasını silip tekrar build yapması gerekiyor
- `config.h`'a WiFi SSID/şifre/IP girilmesi gerekiyor

**GCC ICE hatası (çözüldü, eklendi):**
```
esp_lcd_panel_rgb.c:700: internal compiler error: Segmentation fault during RTL pass: ira
```
ESP-IDF 5.5.3 + GCC 14.2.0 kombinasyonunda bilinen derleyici bug'ı. `-O0` ile geçiliyor.

---

### 🔲 FAZA 2 — Ollama VLM Entegrasyonu

**Kapsam:** `simple_llm()` placeholder'ı gerçek LLM ile değiştir

**Yapılacaklar:**
- `server/llm.py` oluştur
- Ollama `gemma3:4b` modeli (`http://localhost:11434/api/generate`)
- Transcript + kullanıcı adı + lab context prompt'u
- Türkçe yanıt üretimi
- `server/main.py`'de `simple_llm()` çağrısını `llm.generate()` ile değiştir

**Notlar:**
- Ollama PC'de local çalışmalı, GPU önerilir
- `gemma3:4b` Türkçe için yeterince iyi, gerekirse `llama3.1:8b`

---

### 🔲 FAZA 3 — Duman Algılama ve Fan Kontrol

**Kapsam:** MQ135 + L298N fan sistemi

**Pinler:**
- MQ135 AOUT → GPIO3 (ADC1_CH2)
- L298N IN4 → GPIO21
- L298N ENA → GPIO18 (LEDC PWM, 25kHz)

**Eşik değerleri:**
| ADC Değeri | Durum | Fan |
|---|---|---|
| < 800 | Temiz | Kapalı |
| 800 – 1500 | Orta | %50 PWM |
| > 1500 | Tehlikeli | %100 PWM + SMOKE_ALERT |

**Yapılacaklar:**
- `main/smoke_sensor.c/h` — ADC1 okuma, eşik kontrolü
- `main/fan_control.c/h` — LEDC PWM init, fan hız ayarı
- `main.c`'ye duman görev ekle (FreeRTOS, 500ms periyot)
- `ui_smartlab.c`'de `UI_SMOKE_ALERT` state zaten var
- `server/smoke_alert.py` — `/smoke_alert` POST endpoint (zaten `main.py`'de var)

---

### 🔲 FAZA 4 — Multimodal + RAG

**Kapsam:** Kamera + ChromaDB PDF bilgi tabanı

**Yapılacaklar:**
- OV2640 kamera modülü entegrasyonu (ESP32-S3 Camera)
- Görüntü JPEG sıkıştırma, WebSocket üzerinden gönderim
- Server: ChromaDB vektör veritabanı
- PDF lab dokümanları → chunk → embed → store
- RAG pipeline: STT transcript + kamera frame + ChromaDB context → LLM

---

### 🔲 FAZA 5 — Tam Entegrasyon ve Test

**Kapsam:** Tüm sistemlerin birlikte çalışması

**Yapılacaklar:**
- End-to-end test: RFID → Ses → STT → LLM (RAG) → TTS
- Latency ölçümü (hedef: <3 saniye)
- Duman senaryosu test
- Hata yönetimi iyileştirmeleri
- Demo video çekimi

---

## İDF Bağımlılıkları (idf_component.yml)

```yaml
dependencies:
  idf: ">=5.1"
  lvgl/lvgl: "~8.3.11"
  espressif/esp_lcd_ili9341: "^1.0"
  espressif/esp_websocket_client: ">=1.1.0"
```

## CMakeLists.txt (main/)

```cmake
idf_component_register(
    SRCS "main.c" "i2s_mic.c" "i2s_player.c" "crypto.c" "ws_client.c" "rfid.c" "ui/display.c" "ui/ui_smartlab.c"
    INCLUDE_DIRS "." "ui"
    REQUIRES esp_wifi nvs_flash esp_event esp_timer driver esp_adc esp_websocket_client mbedtls lvgl esp_lcd_ili9341
)
```

---

## Kritik Tasarım Kararları

1. **SPI bus paylaşımı:** ILI9341 ve MFRC522 aynı SPI bus'ı paylaşır, farklı CS pinleri ile.
2. **PSRAM kullanımı:** Ses buffer'ları (`s_accum`, `s_plain`) `heap_caps_malloc(MALLOC_CAP_SPIRAM)` ile PSRAM'dan alınır.
3. **LVGL Core 1'e pin:** `display.c`'de LVGL task `xTaskCreatePinnedToCore(..., 1)` ile Core 1'de çalışır. WiFi ve WebSocket Core 0'da.
4. **LVGL mutex:** `lv_port_sem` ile LVGL çağrıları korunur.
5. **DMA callback kaldırıldı:** `esp_lcd_panel_io_register_event_callbacks` ESP-IDF versiyonlarında API değişikliği var. Senkron `lv_disp_flush_ready()` tercih edildi.
6. **Optimizasyon -O0:** GCC 14.2.0 bug'ı nedeniyle `CONFIG_COMPILER_OPTIMIZATION_NONE=y`.

---

## Sunucu Başlatma

```bash
cd iot-project-final-v1/server
pip install fastapi uvicorn websockets faster-whisper
uvicorn main:app --host 0.0.0.0 --port 8080
```

## ESP32 Build & Flash

```bash
cd iot-project-final-v1
# İlk seferinde veya sorun olursa:
rm -rf build && rm -f sdkconfig
idf.py build
idf.py -p COMX flash monitor   # COMX = Device Manager'dan bak
```

---

## RFID Kullanıcı Tablosu (server/session.py)

```python
RFID_USERS = {
    "A1B2C3D4": "Abdulsamet Kara",
    "B2C3D4E5": "Test Kullanıcı 2",
}
```
Yeni kullanıcı eklemek için bu dict'e satır ekle.

---

## Sıradaki Adım

**Şu an:** Faza 1 kodu yazıldı. Build için:
1. `config.h`'a WiFi SSID + şifre + PC IP gir
2. `sdkconfig.defaults`'ta `CONFIG_COMPILER_OPTIMIZATION_NONE=y` var ✓
3. `build/` ve `sdkconfig` sil
4. `idf.py build` çalıştır
5. Başarılıysa `idf.py flash monitor` ile ESP32'ye yükle
6. Sunucuyu başlat, RFID kart tut, PTT butonuna bas, test et

**Faza 1 test sonrası:** Faza 2 (Ollama LLM entegrasyonu).
