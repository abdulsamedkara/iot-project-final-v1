# SmartLab Asistan — CLAUDE.md
> ESP32-S3 tabanlı AI destekli laboratuvar asistanı. Tüm fazlar yazıldı, flash + test aşamasındayız.

---

## Proje Özeti

**SmartLab Asistan** — ESP32-S3 üzerinde çalışan, sesli komutla etkileşime giren, RFID ile kullanıcı tanıyan, duman algılayan ve fan kontrol eden IoT final projesi.

- **Donanım:** ESP32-S3 N16R8 (16MB Flash, 8MB Octal PSRAM)
- **Framework:** ESP-IDF 5.5.3, C dili
- **Server:** Python 3.11+, FastAPI, Uvicorn
- **Dil:** İngilizce (STT, TTS, LLM hepsi İngilizce)
- **Öğrenci:** Abdulsamet Kara

---

## Donanım Listesi ve PIN Tablosu

| Bileşen | Arayüz | Pinler |
|---|---|---|
| ILI9341 TFT (240×320) | SPI | MOSI=11, MISO=13, CLK=12, CS=9, DC=8, RST=7, BL=46 |
| MFRC522 RFID | SPI (aynı bus) | CS=10, RST=yazılımsal (-1) |
| INMP441 Mikrofon | I2S | SCK=42, WS=2, SD=41 |
| MAX98357A Hoparlör | I2S | BCK=16, WS=17, DIN=15 |
| MQ135 Duman Sensörü | ADC | AOUT → GPIO4 (ADC1_CH3) |
| L298N Fan Sürücü | LEDC PWM | IN4=GPIO21, ENA=GPIO18 |
| PTT Butonu | GPIO | GPIO0 (BOOT butonu — karta lehimli) |
| DHT11 Sıcaklık/Nem | 1-Wire | DATA=GPIO47 |
| LDR Işık Sensörü | ADC | AOUT → GPIO6 (ADC1_CH5) |
| PIR Hareket Sensörü | GPIO | OUT=GPIO5 |
| Alev Sensörü | GPIO | DO=GPIO48 (LOW=alev algılandı) |
| SW-420 Titreşim | GPIO | DO=GPIO1 |

**Kritik notlar:**
- ILI9341 ve MFRC522 **aynı SPI2 bus** paylaşır, CS pinleri farklı (CS=9 vs CS=10)
- MQ135: sadece **AOUT** pini bağla → **GPIO4** (GPIO3 strapping pin, kullanma!)
- L298N: ENA=GPIO18 (PWM hız), IN4=GPIO21 (yön), IN3'ü GND'ye çek
- GPIO0 = BOOT butonu = PTT. Flash sırasında basılı tutma.
- RFID RST = yazılımsal reset (fiziksel RST pini bağlamaya gerek yok, -1)
- DHT11 okuma sırasında `vTaskSuspendAll()` çağrılır (~7ms) — WiFi'yi kısa süre durdurur, normaldir

---

## Dosya Yapısı (güncel)

```
iot-project-final-v1/
├── main/
│   ├── CMakeLists.txt          ← smoke_sensor.c + fan_control.c dahil
│   ├── idf_component.yml       ← LVGL, ILI9341, WebSocket bağımlılıkları
│   ├── config.h                ← WiFi/IP/Pin tanımları — BU DOSYAYI DOLDUR
│   ├── main.c                  ← Ana akış, FreeRTOS görevleri, smoke_task
│   ├── crypto.c / crypto.h     ← AES-256-CBC (mbedTLS hardware)
│   ├── ws_client.c / ws_client.h   ← WebSocket istemci, frame birleştirme
│   ├── rfid.c / rfid.h         ← MFRC522 bare-metal SPI driver
│   ├── i2s_mic.c / i2s_mic.h  ← INMP441 mikrofon
│   ├── i2s_player.c / i2s_player.h ← MAX98357A hoparlör
│   ├── smoke_sensor.c / smoke_sensor.h  ← MQ135 ADC + LDR ADC (ldr_sensor_init/read_avg)
│   ├── fan_control.c / fan_control.h    ← L298N LEDC PWM
│   ├── dht11.c / dht11.h               ← DHT11 1-Wire sürücü (vTaskSuspendAll tabanlı)
│   └── ui/
│       ├── display.c / display.h        ← LVGL 8.3 + ILI9341 init
│       └── ui_smartlab.c / ui_smartlab.h ← 8 durumlu TFT state machine
├── server/
│   ├── main.py                 ← FastAPI: WebSocket + REST + static files
│   ├── session.py              ← SessionStore, RFID_USERS dict
│   ├── crypto.py               ← Python AES-256-CBC
│   ├── stt.py                  ← Faster-Whisper STT (base, GPU, English)
│   ├── tts.py                  ← Piper TTS (en_US-lessac-medium, 22050Hz)
│   ├── llm.py                  ← Ollama istemcisi (gemma3:4b, English)
│   ├── rag.py                  ← ChromaDB RAG (PDF → chunk → embed → query)
│   ├── requirements.txt        ← Tüm Python bağımlılıkları
│   ├── tts_models/             ← en_US-lessac-medium.onnx + .onnx.json (indirildi)
│   ├── chroma_db/              ← ChromaDB kalıcı depolama (otomatik oluşur)
│   └── knowledge_base/         ← PDF'leri buraya koy (şu an boş)
├── web_ui/
│   └── index.html              ← Mobil web UI (Claude Design export)
├── iot-web/
│   └── smartlab-asistan.html   ← Orijinal tasarım dosyası (kaynak)
├── partitions.csv              ← Özel partition tablosu (3MB factory)
├── sdkconfig.defaults          ← ESP32-S3 build konfigürasyonu
└── CLAUDE.md                   ← Bu dosya
```

---

## FAZA DURUMU (GÜNCEL)

### ✅ FAZA 1 — Temel İletişim (KOD TAMAM, FLASH BEKLİYOR)

WebSocket bağlantısı + AES-256-CBC şifreleme + RFID okuma + TFT state machine.

**Yapılanlar:**
- `main.c` — WiFi init, SPI bus paylaşımı, tüm modül init, RFID+PTT ana döngüsü
- `crypto.c/h` — AES-256-CBC encrypt/decrypt, hardware RNG, PKCS#7 padding
- `ws_client.c/h` — WebSocket client, session key exchange, PSRAM'da frame birleştirme
- `rfid.c/h` — MFRC522 bare-metal SPI (REQA → AntiColl → UID → HLTA)
- `ui/display.c/h` — LVGL 8.3 init, ILI9341 SPI flush, Core 1'e pin
- `ui/ui_smartlab.c/h` — 8 durumlu TFT state machine
- `sdkconfig.defaults` — ESP32-S3 N16R8, 16MB flash, Octal PSRAM, HW AES
- `partitions.csv` — Özel partition tablosu: 3MB factory (binary 1.78MB sığıyor)

**Build sorunu (çözüldü):**
```
GCC 14.2.0 ICE: esp_lcd_panel_rgb.c:700: internal compiler error
→ Fix: CONFIG_COMPILER_OPTIMIZATION_NONE=y (sdkconfig.defaults'ta var)
```

**Build edildi:** Binary 1.78MB, 3MB factory partition'da %43 dolu.

**Flash için:**
```powershell
# config.h'ı doldur (aşağıya bak)
# Sonra:
idf.py -p COM3 flash monitor   # COM numarasını Device Manager'dan bak
```

---

### ✅ FAZA 2 — Ollama LLM Entegrasyonu (TAMAM)

`server/llm.py` — Ollama `gemma3:4b` modeli ile İngilizce yanıt üretir.

**Özellikler:**
- Model: `gemma3:4b` — `http://localhost:11434/api/chat`
- System prompt: İngilizce, lab asistanı rolü, 2-4 cümle kısa yanıtlar
- RAG context + görüntü (base64) desteği var
- Ollama erişilemezse kural tabanlı fallback yanıtlar döner

**Ollama kurulum:**
```bash
# ollama.com'dan indir, kur, sonra:
ollama pull gemma3:4b
ollama serve   # her server başlatmadan önce çalışmalı
```

---

### ✅ FAZA 3 — Sensörler ve Fan Kontrol (TAMAM)

`smoke_sensor.c/h` + `fan_control.c/h` + `dht11.c/h` firmware'de mevcut.

**MQ135 Duman eşik değerleri (config.h):**
| ADC Değeri | Durum | Fan |
|---|---|---|
| < 800 | Temiz hava | Kapalı |
| 800 – 1500 | Orta seviye | %50 PWM |
| > 1500 | Tehlikeli | %100 PWM + UI_SMOKE_ALERT |

**FreeRTOS task'ları (hepsi Core 0):**
| Task | Periyot | Notlar |
|---|---|---|
| `smoke_task` | 500ms | 30s warmup, 3 okuma debounce |
| `dht11_task` | 2000ms | `vTaskSuspendAll` ~7ms kullanır |
| `ldr_task` | 1000ms | ADC ortalama okuma |
| `pir_task` | 500ms | Durum değişiminde log |
| `flame_task` | 500ms | LOW = alev algılandı |
| `vib_task` | 100ms | SW-420, anlık titreşim |

**Server:** `POST /smoke_alert` endpoint mevcut — İngilizce sesli uyarı üretir.

---

### ✅ FAZA 4 — RAG + Fotoğraf Yükleme (KOD TAMAM, YAPILANDIRMA GEREKİYOR)

**RAG (`server/rag.py`):**
- ChromaDB PersistentClient, `paraphrase-multilingual-MiniLM-L12-v2` embeddings
- PDF'leri `rag_pdf/` klasöründen okur, chunk'lar, indeksler
- Sunucu başlarken otomatik indeksleme yapar

**⚠️ Şu an RAG çalışmıyor:** Keras 3 uyumsuzluk hatası.
```
Fix: pip install tf-keras
```
Sonra `rag_pdf/` klasörüne lab PDF'lerini koy, sunucuyu yeniden başlat.

**Fotoğraf yükleme:**
- `POST /api/session/{id}/photo` — web UI'dan gelen fotoğraf
- `POST /upload/{id}` — eski alias
- Fotoğraf base64 olarak session'a bağlanır, LLM'e gönderilir (vision)

---

### ✅ FAZA 5 — Web UI ve Tam Entegrasyon (TAMAM)

**Web UI** — `http://<PC_IP>:8080/` adresinde çalışıyor.

**3 ekran:**
1. **RFID Bekleme** — animasyonlu RFID halkaları, ESP32 kart okuyunca otomatik geçiş
2. **Fotoğraf Yükle** — kamera ile çek veya galeriden seç, sunucuya yükle
3. **Oturum Durumu** — session ID, kullanıcı, süre, fotoğraf durumu

**RFID event akışı (gerçek zamanlı):**
- ESP32 → Server `/ws` (JSON RFID frame) → Server `_broadcast_rfid()` → Web UI `/ws/rfid` WebSocket
- Fallback: `/api/rfid/pending` polling (WebSocket başarısız olursa)

**Demo butonu:** `POST /api/demo/session` → gerçek session oluşturur, tam flow test edilebilir.

**Server tüm REST endpoint'leri:**
```
WS   /ws                        ← ESP32 ana bağlantısı
WS   /ws/rfid                   ← Web UI RFID push stream
GET  /api/rfid/pending          ← Web UI RFID poll fallback
POST /api/demo/session          ← Demo session oluştur
POST /api/session/{id}/photo    ← Fotoğraf yükle (web UI)
POST /upload/{id}               ← Fotoğraf yükle (alias)
POST /smoke_alert               ← Duman uyarısı (ESP32)
GET  /sessions                  ← Aktif session listesi
GET  /health                    ← Durum kontrolü
GET  /                          ← Web UI (static files)
```

---

## config.h — DOLDURULACAK SATIRLAR

```c
// Bu 3 satırı kendi bilgilerine göre değiştir:
#define WIFI_SSID     "YOUR_WIFI_SSID"      // WiFi ağ adı
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"   // WiFi şifresi
#define SERVER_HOST   "192.168.1.XXX"        // PC'nin yerel IP'si

// ipconfig (Windows) veya ip addr (Linux) ile PC IP'sini bul
```

---

## İletişim Protokolü

### Bağlantı (Server → ESP32, JSON):
```json
{"type": "session", "session_id": "...", "key_b64": "<base64 AES-256 key>"}
```

### RFID (ESP32 → Server, JSON):
```json
{"type": "rfid", "uid": "A1B2C3D4", "session_id": "..."}
```

### Server kullanıcı adı (Server → ESP32, JSON):
```json
{"type": "user", "name": "Abdulsamet Kara"}
```

### Ses gönderimi (ESP32 → Server, Binary):
```
[0x01][IV 16 byte][AES-256-CBC şifreli PCM]
```

### Ses yanıtı (Server → ESP32, Binary):
```
[IV 16 byte][AES-256-CBC şifreli PCM]
```

> **Fragmentation:** Büyük ses yanıtları (200–400KB) birden fazla frame'de gelir.
> `ws_client.c` içinde PSRAM buffer `s_accum`'da toplanır, `fin` flag'i gelince işlenir.

---

## AES-256-CBC

- **Anahtar:** 32 byte, sunucu her oturumda üretir, base64 JSON ile gönderir
- **IV:** Her mesajda 16 byte, `esp_fill_random` ile üretilir, frame başına eklenir
- **Padding:** PKCS#7, `crypto_pad_pcm()` ile
- **Hardware:** `CONFIG_MBEDTLS_HARDWARE_AES=y` (ESP32-S3 yerleşik AES birimi)

---

## TFT State Machine

| State | Renk | Açıklama |
|---|---|---|
| `UI_IDLE` | Gri | WiFi/WS bekliyor |
| `UI_RFID_READ` | Mavi | Kart okundu, kullanıcı bekleniyor |
| `UI_READY` | Yeşil | Oturum açık, PTT bekliyor |
| `UI_RECORDING` | Kırmızı | PTT basılı, kayıt var |
| `UI_PROCESSING` | Sarı | Sunucuda işleniyor |
| `UI_SPEAKING` | Cyan | TTS çalıyor |
| `UI_SMOKE_ALERT` | Kırmızı BG | "SMOKE DETECTED!" |
| `UI_ERROR` | Koyu kırmızı | Hata mesajı |

---

## RFID Kullanıcı Tablosu

`server/session.py` içinde `RFID_USERS` dict'i:

```python
RFID_USERS = {
    "A1B2C3D4": "Abdulsamet Kara",
    "11223344": "Test Kullanicisi",
    # Yeni kart: "HEXUID": "Ad Soyad"
}
```

RFID UID'yi öğrenmek için ESP32'yi flash'la ve monitor'da `RFID:` satırını izle.

---

## Kritik Tasarım Kararları

1. **SPI bus paylaşımı:** ILI9341 + MFRC522 aynı SPI2_HOST, farklı CS (GPIO10 / GPIO7)
2. **PSRAM:** Ses buffer'ları `heap_caps_malloc(MALLOC_CAP_SPIRAM)` — stack'te sığmaz
3. **LVGL Core 1:** `xTaskCreatePinnedToCore(..., 1)` — WiFi/WS Core 0'da, LVGL Core 1'de
4. **LVGL mutex:** `lv_port_sem` ile tüm LVGL çağrıları korunuyor
5. **DMA callback yok:** `esp_lcd_panel_io_register_event_callbacks` API uyumsuzluğu var, senkron `lv_disp_flush_ready()` kullanıldı
6. **-O0 optimizasyon:** GCC 14.2.0 ICE bug'ı — `CONFIG_COMPILER_OPTIMIZATION_NONE=y`
7. **Partition tablosu:** Binary 1.78MB, varsayılan 1.5MB'a sığmıyor. `partitions.csv` ile 3MB factory partition

---

## Sunucu Kurulum (sıfırdan)

```powershell
cd server

# Bağımlılıkları kur
pip install -r requirements.txt
pip install tf-keras   # RAG için Keras uyumsuzluk fix

# Piper TTS modeli zaten indirildi: tts_models/en_US-lessac-medium.onnx
# Whisper modeli ilk çalıştırmada indirilir (~140MB)

# Ollama kur (ollama.com) ve modeli çek
ollama pull gemma3:4b

# Sunucuyu başlat (her seferinde ollama da çalışmalı)
ollama serve                                    # ayrı terminal
uvicorn main:app --host 0.0.0.0 --port 8080    # server terminali
```

**Web UI:** `http://<PC_IP>:8080/` — herhangi bir cihazdaki tarayıcıdan açılır.

---

## ESP32 Build & Flash

```powershell
# ESP-IDF ortamı aktif olmalı (VS Code'da otomatik)
cd iot-project-final-v1

# İlk seferinde veya sorun olursa:
Remove-Item -Recurse -Force build
Remove-Item -Force sdkconfig

# Build (config.h doluysa):
idf.py build

# Flash:
idf.py -p COM3 flash monitor   # COM numarasını Device Manager > Ports'tan bak
```

---

## Test Senaryosu (sırayla)

1. Sunucuyu başlat (`ollama serve` + `uvicorn ...`)
2. ESP32'yi flash'la ve monitörü izle — WiFi bağlanmalı, WS bağlantısı kurulmalı
3. TFT'de `UI_IDLE` → `UI_READY` geçişi bekleniyor (WiFi + WS sonrası)
4. RFID kartı okuyucuya tut → TFT `UI_RFID_READ` → server kullanıcı adı döner → `UI_READY`
5. Web UI'da `http://<PC_IP>:8080/` aç → RFID okununca otomatik geçiş bekleniyor
6. PTT butonuna (GPIO0) bas, konuş, bırak → `UI_RECORDING` → `UI_PROCESSING` → `UI_SPEAKING`
7. Cevap İngilizce gelir, hoparlörden çıkar
8. MQ135'e gaz tut → ADC > 1500 → fan döner → TFT `UI_SMOKE_ALERT`

---

## Kalan Yapılacaklar

| Görev | Açıklama |
|---|---|
| `config.h` doldur | WiFi SSID + şifre + PC IP |
| `idf.py flash` | Firmware ESP32'ye yükle |
| `ollama pull gemma3:4b` | LLM modelini indir |
| `pip install tf-keras` | RAG embedding fix |
| PDF ekle | `rag_pdf/` klasörüne lab dokümanları |
| RFID tablosuna kart ekle | `session.py` > `RFID_USERS` dict |
| End-to-end test | Yukarıdaki test senaryosunu çalıştır |

---

## İDF Bağımlılıkları (idf_component.yml)

```yaml
dependencies:
  idf: ">=5.1"
  lvgl/lvgl: "~8.3.11"
  espressif/esp_lcd_ili9341: "^1.0"
  espressif/esp_websocket_client: ">=1.1.0"
```

## CMakeLists.txt (main/) — güncel

```cmake
idf_component_register(
    SRCS "main.c" "dht11.c" "i2s_mic.c" "i2s_player.c" "crypto.c" "ws_client.c"
         "rfid.c" "smoke_sensor.c" "fan_control.c"
         "ui/display.c" "ui/ui_smartlab.c"
    INCLUDE_DIRS "." "ui"
    REQUIRES esp_wifi nvs_flash esp_event esp_timer driver esp_adc
             esp_websocket_client mbedtls lvgl esp_lcd_ili9341
)
```
