# SmartLab Asistanı — Final Proje Taslağı
### Yapay Zeka Destekli, Çok Modlu Akıllı Atölye Yönetim Sistemi

---

## 1. Proje Özeti

SmartLab Asistanı; elektromekanik atölyeler, maker laboratuvarları ve teknik çalışma alanlarında **güvenliği artırmak**, cihaz kullanım oryantasyonunu dijitalleştirmek ve kullanıcılara anlık teknik destek sağlamak amacıyla geliştirilmiş **IoT + Yerel Yapay Zeka (Local AI)** entegrasyonlu bir sistemdir.

Temel çalışma prensibi:
- Kullanıcı RFID kartıyla oturum açar
- Anlamadığı cihazın fotoğrafını mobil arayüz üzerinden yükler
- ESP32 üzerindeki "Bas-Konuş" butonuna basarak sorusunu söyler
- Sistem sesi metne çevirir, fotoğraf + metin + PDF kılavuz bilgisini birleştirerek sesli yanıt verir
- Tüm bu süreç **yerel ağda**, **internet bağlantısı olmadan** ve **AES-256 şifreli** iletişimle gerçekleşir

---

## 2. Sistem Mimarisi

```
┌──────────────────────────────────────────────────────────────┐
│                      ESP32-S3 (Edge Node)                    │
│                                                              │
│  [MFRC522 RFID] ──SPI──┐                                    │
│  [ILI9341 TFT]  ──SPI──┤── ESP32-S3 N16R8                   │
│  [INMP441 Mic]  ──I2S──┤   (16MB Flash, 8MB PSRAM)          │
│  [MAX98357A Amp]──I2S──┤                                     │
│  [PTT Butonu]   ──GPIO─┘                                     │
│                         │                                    │
│              Wi-Fi / WSS (TLS 1.3 + AES-256)                │
└─────────────────────────┼────────────────────────────────────┘
                          │
┌─────────────────────────▼────────────────────────────────────┐
│                   LOCAL AI SERVER (PC)                       │
│                                                              │
│  FastAPI + WebSocket Handler                                 │
│       │                                                      │
│       ├── AES-256 Deşifre                                    │
│       ├── Faster-Whisper (base) ──── STT                     │
│       ├── gemma3:4b / qwen2-vl:2b ── VLM (Ollama)           │
│       ├── ChromaDB + PDF ─────────── RAG                     │
│       └── Piper TTS ──────────────── Streaming Ses           │
│                                                              │
│  ┌─────────────────────────────────┐                         │
│  │   Mobil Web Arayüzü (PWA)       │                         │
│  │   Fotoğraf yükleme + Session ID │                         │
│  └─────────────────────────────────┘                         │
└──────────────────────────────────────────────────────────────┘
```

---

## 3. Donanım Bileşenleri

| Bileşen | Model | Arayüz | Görev |
|---------|-------|--------|-------|
| Mikrodenetleyici | ESP32-S3 N16R8 | — | Merkezi kontrol |
| Ekran | ILI9341 2.4" TFT (240x320) | SPI | Durum gösterimi |
| RFID Okuyucu | MFRC522 | SPI | Kullanıcı tanıma |
| Mikrofon | INMP441 | I2S | Ses girişi |
| Amfi + Hoparlör | MAX98357A | I2S | Ses çıkışı |
| Gaz/Duman Sensörü | MQ135 | ADC (AOUT) | CO₂/duman tespiti |
| Motor Sürücü + Fan | L298N + DC Fan | GPIO + PWM | Duman dağıtma |
| Buton | Anlık basmalı | GPIO | Push-to-Talk |

### Pin Haritası (ESP32-S3)

```
── SPI Bus (MFRC522 + ILI9341 paylaşımlı) ──
MOSI        → GPIO11
MISO        → GPIO13
SCK         → GPIO12
MFRC522 CS  → GPIO10
ILI9341 CS  → GPIO9
ILI9341 DC  → GPIO8
ILI9341 RST → GPIO7
ILI9341 BL  → GPIO46  (PWM ile parlaklık kontrolü)

── I2S Bus 0 (INMP441 Mikrofon) ──
WS          → GPIO4
SCK         → GPIO5
SD          → GPIO6

── I2S Bus 1 (MAX98357A Amfi) ──
WS          → GPIO15
BCK         → GPIO16
DIN         → GPIO17

── GPIO ──
PTT Butonu  → GPIO0  (INPUT_PULLUP, LOW = basılı)

── MQ135 Gaz/Duman Sensörü ──
AOUT        → GPIO3  (ADC1_CH2 — analog gaz yoğunluğu)
DOUT        → GPIO14 (dijital eşik çıkışı — isteğe bağlı)

── L298N Fan Motor Sürücü ──
IN4 (yön)   → GPIO21 (HIGH = fan çalışır)
ENA (hız)   → GPIO18 (LEDC PWM — TIMER_1 / CHANNEL_1, 25 kHz)
```

> **Not:** MFRC522 ve ILI9341 aynı SPI veri hatlarını paylaşır. Her ikisi de farklı CS (Chip Select) piniyle seçildiğinden çakışma olmaz. Bu standart SPI multi-device konfigürasyonudur.
>
> **Pin Çakışma Notu:** Vize projesinde `FAN_IN4 = GPIO7` kullanılıyordu. Final projesinde GPIO7 ILI9341 RST'a atandığından fan yön pini **GPIO21**'e taşınmıştır.

---

## 4. Yazılım Yığını (Software Stack)

### 4.1 ESP32 Firmware (Arduino / ESP-IDF)

```
Kütüphaneler:
├── TFT_eSPI          → ILI9341 ekran sürücüsü (sprite desteği)
├── MFRC522           → RFID okuyucu
├── driver/i2s.h      → Mikrofon ve amfi (ESP-IDF native)
├── esp_adc           → MQ135 analog okuma (ADC1_CH2 / GPIO3)
├── driver/ledc.h     → Fan PWM hız kontrolü (LEDC_TIMER_1)
├── WiFiClientSecure  → TLS/WSS bağlantısı
├── WebSocketsClient  → WebSocket yönetimi (arduinoWebSockets)
└── mbedtls/aes.h     → AES-256 hardware şifreleme
```

**Firmware Ana Döngüsü:**

```
1. Boot → WiFi bağlan → Sunucuya WSS bağlantısı kur
2. RFID kart beklenir (IDLE)
3. Kart okununca → UID sunucuya gönder → Session key al
4. Kullanıcı PTT'ye basar → I2S mic başlar → ses chunk'ları AES şifrele → WS gönder
5. PTT bırakılınca → END_OF_AUDIO sinyali gönder
6. Sunucudan AES şifreli ses paketi gelir → deşifre → I2S amfiye besle
7. TFT ekran her adımda state machine ile güncellenir
```

### 4.2 Sunucu Backend (Python)

```
Kütüphaneler:
├── FastAPI + uvicorn    → HTTP + WebSocket sunucu
├── faster-whisper       → Yerel STT motoru
├── ollama (Python SDK)  → VLM erişimi
├── chromadb             → Vektör veri tabanı (RAG)
├── sentence-transformers→ PDF embedding
├── piper-tts            → Yerel TTS motoru
├── cryptography         → AES-256 (Fernet/hazmat)
└── Pillow               → Görsel ön işleme
```

---

## 5. Yapay Zeka Pipeline'ı

### 5.1 Model Seçimi (4GB VRAM için optimize)

| Model | Görev | VRAM | Neden? |
|-------|-------|------|--------|
| `gemma3:4b` (Ollama) | VLM (görsel + metin) | ~3GB Q4 | Vision destekli, güçlü reasoning |
| `faster-whisper base` | STT | ~0.5GB | GPU hızlandırmalı, Türkçe destekli |
| `piper TTS` | TTS | CPU | Çok hızlı, yerel, akıcı ses |

> **Alternatif:** `qwen2-vl:2b-q4_K_M` — ~2.5GB VRAM, gemma3:4b yoksa iyi yedek.  
> **Ollama kontrolü:** `ollama search gemma4` → Gemma 4 mevcutsa ve vision destekliyorsa tercih edin.

### 5.2 Streaming Pipeline (Gecikme Optimizasyonu)

```
Hedef: Kullanıcı konuşmayı bitirince ilk ses yanıtı ≤ 2 saniyede başlamalı

[PTT bırakıldı]
      │
      ▼  ~300ms
[Faster-Whisper: base, GPU]  →  "Akım limitini nasıl ayarlarım?"
      │
      ▼  ~200ms
[Session'dan fotoğraf context alınır + RAG sorgusu]
      │
      ▼  ~500ms (ilk token)
[gemma3:4b - STREAMING modu]
      │
      ├── "The current limit button is..."  (1. cümle tamamlandı)
      │         │
      │         ▼  ~100ms
      │   [Piper TTS → 1. cümle ses chunk'ı]
      │         │
      │         ▼
      │   [ESP32'ye gönder → HOPARLÖR BAŞLAR] ← kullanıcı burada duyar
      │
      ├── "...located at the bottom left."  (2. cümle)
      │         ▼
      │   [Piper TTS → 2. cümle] → ESP32 (oynatmaya devam)
      └── ...
```

**Toplam algılanan gecikme:** ~1.5–2s

### 5.3 Streaming Sunucu Kodu (Özet)

```python
@app.websocket("/ws/{session_id}")
async def websocket_endpoint(websocket: WebSocket, session_id: str):
    await websocket.accept()
    session = session_store.get(session_id)
    
    while True:
        # 1. Şifreli ses al
        data = await websocket.receive_bytes()
        audio = aes256_decrypt(data, session.key)
        
        # 2. STT
        text = whisper_model.transcribe(audio)["text"]
        
        # 3. Context: fotoğraf + RAG
        image = session.get_image()                    # önceden yüklü
        rag_context = chromadb_query(text)             # PDF kılavuz
        
        # 4. VLM streaming
        stream = ollama.chat(
            model="gemma3:4b",
            messages=build_prompt(text, image, rag_context),
            stream=True
        )
        
        # 5. Cümle bazlı TTS + gönderim
        buffer = ""
        async for chunk in stream:
            buffer += chunk["message"]["content"]
            if buffer.endswith((".", "!", "?")):
                audio_chunk = piper_tts.synthesize(buffer)
                encrypted = aes256_encrypt(audio_chunk, session.key)
                await websocket.send_bytes(encrypted)
                buffer = ""
```

---

## 6. Duman Algılama ve Fan Kontrol Sistemi

### 6.1 MQ135 Çalışma Mantığı

MQ135, ısındıkça iç direnci değişen bir metal oksit sensördür. CO₂, NH₃, alkol, benzen gibi zararlı gazları ve dumanı algılar. ESP32-S3 ADC'si ile analog yoğunluk değeri sürekli okunur.

```
Gaz Yoğunluğu → Sensör Direnci Düşer → AOUT Voltajı Artar → ADC Değeri Yükselir
```

**Eşik Değerleri (0–4095 ADC aralığı):**

| ADC Değeri | Durum | Eylem |
|------------|-------|-------|
| < 800 | Temiz hava | Fan kapalı |
| 800 – 1500 | Hafif gaz/duman | Fan %50 hızda çalışır |
| > 1500 | Yoğun duman | Fan %100 — TFT: SMOKE_ALERT — Sesli uyarı |

> **Isınma Süresi:** MQ135'in kararlı ölçüm yapabilmesi için açılışta ~30 saniye ısınma beklenmeli. Bu sürede fan ve uyarı sistemi devre dışı bırakılmalıdır (`warmup_complete` flag).

### 6.2 Fan Tepki Akışı

```
[Her SENSOR_UPDATE_MS'de MQ135 okur]
          │
          ▼
    ADC < 800?  ──YES──→  [Fan OFF]
          │
          NO
          ▼
    ADC < 1500? ──YES──→  [Fan %50 PWM] → TFT: "Havalandırılıyor..."
          │
          NO
          ▼
    ADC ≥ 1500  ──────→  [Fan %100 PWM]
                          [TFT: SMOKE_ALERT — Kırmızı arka plan]
                          [Sunucuya POST /smoke_alert]
                          [Piper TTS: "Dikkat! Duman tespit edildi.
                           Lütfen pencereyi açın."]
                          [Sesli uyarı ESP32 hoparlöründen çalar]
```

### 6.3 PWM Fan Hız Kontrolü (L298N)

Vizede geliştirilen sıcaklık bazlı `fan_update()` fonksiyonu, duman yoğunluğuna göre uyarlanır:

```c
// smoke_sensor.h — eşik sabitleri
#define SMOKE_ADC_CLEAR      800    // Temiz hava eşiği
#define SMOKE_ADC_ALERT      1500   // Yoğun duman eşiği
#define SMOKE_WARMUP_MS      30000  // Isınma süresi (ms)
#define SMOKE_SAMPLE_COUNT   32     // Gürültü filtreleme için ortalama

// fan_control.h — PWM sabitleri (vizeden taşındı, pin güncellemesiyle)
#define FAN_IN4_GPIO         21     // L298N yön pini (GPIO7'den taşındı)
#define FAN_ENA_GPIO         18     // L298N PWM hız pini
#define FAN_LEDC_TIMER       LEDC_TIMER_1
#define FAN_LEDC_CHANNEL     LEDC_CHANNEL_1
#define FAN_PWM_FREQ_HZ      25000  // 25 kHz — motor sessiz çalışma frekansı
#define FAN_DUTY_RES         LEDC_TIMER_8_BIT   // 0–255
#define FAN_DUTY_HALF        128    // %50 hız
#define FAN_DUTY_FULL        255    // %100 hız
```

```c
// fan_control.c — duman bazlı fan güncelleme
void fan_update_smoke(int adc_value) {
    if (adc_value < SMOKE_ADC_CLEAR) {
        gpio_set_level(FAN_IN4_GPIO, 0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL, 0);
    } else if (adc_value < SMOKE_ADC_ALERT) {
        gpio_set_level(FAN_IN4_GPIO, 1);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL, FAN_DUTY_HALF);
    } else {
        gpio_set_level(FAN_IN4_GPIO, 1);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL, FAN_DUTY_FULL);
    }
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL);
}
```

### 6.4 Sunucu Taraflı Sesli Uyarı

```python
# server/smoke_alert.py
@app.post("/smoke_alert")
async def smoke_alert(req: Request):
    body = await req.json()
    adc_val = body.get("adc_value", 0)
    
    if adc_val >= SMOKE_ADC_ALERT:
        alert_text = "Dikkat! Laboratuvarda duman tespit edildi. Lütfen pencereyi açın ve çalışma alanını havalandırın."
    else:
        alert_text = "Hafif duman veya gaz algılandı. Havalandırma sistemi devreye girdi."
    
    audio_bytes = piper_tts.synthesize(alert_text)
    return Response(content=audio_bytes, media_type="application/octet-stream")
```

---

## 7. RAG Sistemi (Retrieval-Augmented Generation)

```
Kurulum (bir kere):
  PDF kılavuzlar → chunk'lara bölünür (500 token) 
                → sentence-transformers ile embedding 
                → ChromaDB'ye kaydedilir

Sorgu sırasında:
  Kullanıcı sorusu → embedding → ChromaDB'de en yakın 3 chunk bulunur
                  → VLM prompt'una eklenir → model "uydurmaz"

Dizin yapısı:
  /knowledge_base/
    ├── power_supply_xyz.pdf
    ├── oscilloscope_abc.pdf
    ├── soldering_station.pdf
    └── ...
```

---

## 8. AES-256 Güvenlik Mimarisi

### İki Katmanlı Şifreleme

**Katman 1 — Taşıma Katmanı (TLS 1.3):**
```
ESP32 ←──── WSS (wss://) ────→ FastAPI
       TLS 1.3 = AES-256-GCM kullanır
       mbedTLS ile ESP32-S3'te doğrudan desteklenir
```

**Katman 2 — Uygulama Katmanı (AES-256-CBC):**
```
[RFID oturumu açılır]
        ↓
[Sunucu → 32-byte rastgele session key üretir]
        ↓
[Key, TLS tüneli üzerinden ESP32'ye iletilir (güvenli kanal)]
        ↓
[Her ses paketi: IV (16 byte) + AES-256-CBC(ses, session_key)]
        ↓
[Oturum bitince key PSRAM'den sıfırlanır (zero-out)]
```

**ESP32-S3 Avantajı:** Donanım AES hızlandırıcısı — şifreleme CPU yükü minimize.

```c
// ESP32 şifreleme (özet)
esp_aes_context ctx;
esp_aes_init(&ctx);
esp_aes_setkey(&ctx, session_key, 256);
// Her pakette yeni rastgele IV
esp_fill_random(iv, 16);
esp_aes_crypt_cbc(&ctx, ESP_AES_ENCRYPT, data_len, iv, plaintext, ciphertext);
// Pakete IV + ciphertext birleştirilerek gönderilir
```

---

## 9. ILI9341 TFT Ekran — Durum Makinesi

```
┌────────────────────────────────────────────────────┐
│              EKRAN DURUMLARI                        │
├──────────────┬─────────────────────────────────────┤
│ IDLE         │ SmartLab logosu + saat + "Kart       │
│              │ okutun" yazısı (beyaz/mavi)          │
├──────────────┼─────────────────────────────────────┤
│ RFID_READ    │ "Hoşgeldin, [Ad Soyad]"              │
│              │ Yeşil arka plan, 2 sn göster         │
├──────────────┼─────────────────────────────────────┤
│ PHOTO_READY  │ Küçük kamera ikonu + "Fotoğraf       │
│              │ alındı, soru sorabilirsiniz"         │
├──────────────┼─────────────────────────────────────┤
│ RECORDING    │ Kırmızı mikrofon ikonu               │
│              │ + Canlı VU meter (I2S seviyesinden)  │
│              │ + "Dinliyorum..."                    │
├──────────────┼─────────────────────────────────────┤
│ PROCESSING   │ Dönen spinner animasyonu             │
│              │ + "Düşünüyor..."                     │
├──────────────┼─────────────────────────────────────┤
│ SPEAKING     │ Hoparlör ikonu                       │
│              │ + Cevap metni kayan yazı (scroll)    │
├──────────────┼─────────────────────────────────────┤
│ SMOKE_ALERT  │ ⚠ Yanıp sönen kırmızı arka plan     │
│              │ + "DUMAN TESPİT EDİLDİ!"            │
│              │ + ADC değeri bar grafiği             │
│              │ + "Fan aktif — Havalandırılıyor"     │
│              │ (Yoğunluk düşünce otomatik kapanır) │
├──────────────┼─────────────────────────────────────┤
│ ERROR        │ Kırmızı arka plan + hata mesajı      │
│              │ (WiFi koptu, sunucu yanıt yok vb.)  │
└──────────────┴─────────────────────────────────────┘
```

**Kütüphane:** `TFT_eSPI` — Sprite desteği ile double-buffer animasyon, takıntısız görüntü.

---

## 10. Uçtan Uca Çalışma Senaryosu

```
ADIM 1 — OTURUM BAŞLATMA
  • Kullanıcı RFID kartını okuttur
  • ESP32: UID → WSS → Sunucu
  • Sunucu: DB'den kullanıcı adı çek, session_id + AES-256 key üret
  • Sunucu: Key → WSS (TLS korumalı) → ESP32 PSRAM'e kaydet
  • TFT: "Hoşgeldin, Samet" ekranı

ADIM 2 — FOTOĞRAF YÜKLEME
  • Kullanıcı telefonda local web UI'ı açar (http://192.168.x.x:8000)
  • Cihazın fotoğrafını çeker, session_id ile sunucuya yükler
  • Sunucu: Görseli session'a context olarak bağlar
  • TFT: "Fotoğraf alındı" ekranı

ADIM 3 — SORU SORMA (Push-to-Talk)
  • Kullanıcı PTT butonuna basar
  • TFT: Mikrofon + VU meter ekranı
  • ESP32: I2S mic → ses chunk'ları → AES-256 şifrele → WSS stream
  • Kullanıcı: "How do I set the current limit to 1 amp?"
  • PTT bırakınca → END_OF_AUDIO sinyali

ADIM 4 — YAPAY ZEKA İŞLEME
  • TFT: "Düşünüyor..." spinner
  • Faster-Whisper (base, GPU): ~300ms → metin
  • ChromaDB RAG: En ilgili PDF paragrafları (~200ms)
  • gemma3:4b (Ollama, stream=True): Fotoğraf + metin + RAG → yanıt üretimi

ADIM 5 — SESLİ YANIT (Streaming)
  • TFT: Kayan cevap metni
  • Piper TTS: Cümle cümle ses chunk'ları → AES şifrele → WSS → ESP32
  • MAX98357A hoparlör: "The current limit knob is on the bottom-left panel..."
  • Kullanıcı ilk sesi ~1.5-2 saniyede duyar
```

---

## 11. Proje Dizin Yapısı

```
smartlab/
├── firmware/                    # ESP32-S3 Arduino kodu
│   ├── main.ino
│   ├── display.h               # TFT state machine (SMOKE_ALERT dahil)
│   ├── audio.h                 # I2S mikrofon + amfi
│   ├── rfid.h                  # MFRC522
│   ├── websocket.h             # WSS bağlantısı
│   ├── crypto.h                # AES-256 işlemleri
│   ├── smoke_sensor.h          # MQ135 ADC okuma + eşik mantığı ★ YENİ
│   └── fan_control.h           # L298N PWM fan kontrolü (vizeden uyarlandı) ★ YENİ
│
├── server/                      # Python backend
│   ├── main.py                 # FastAPI + WebSocket
│   ├── stt.py                  # Faster-Whisper
│   ├── vlm.py                  # Ollama VLM streaming
│   ├── rag.py                  # ChromaDB + embeddings
│   ├── tts.py                  # Piper TTS
│   ├── crypto.py               # AES-256 sunucu tarafı
│   ├── session.py              # Session yönetimi
│   ├── smoke_alert.py          # /smoke_alert endpoint + sesli uyarı ★ YENİ
│   └── knowledge_base/         # PDF kılavuzlar
│       ├── power_supply.pdf
│       └── ...
│
├── web_ui/                      # Mobil fotoğraf yükleme
│   ├── index.html
│   └── app.js
│
└── docs/
    └── architecture.md
```

---

## 12. Geliştirme Yol Haritası

```
FAZA 1 — Temel İletişim (1-2 hafta)
  □ FastAPI WebSocket sunucu kurulumu
  □ ESP32 → WSS bağlantısı + AES-256 handshake
  □ PTT → ses akışı → sunucu (ham ses)
  □ TFT basic state machine (IDLE, RECORDING)

FAZA 2 — AI Pipeline (2-3 hafta)
  □ Faster-Whisper STT entegrasyonu
  □ Piper TTS + ses geri akışı
  □ Sadece sesli Q&A test et (görsel olmadan)
  □ Streaming gecikme ölçümü (<2s hedef)

FAZA 3 — Duman Algılama + Fan Sistemi (1 hafta) ★ YENİ
  □ MQ135 ısınma + ADC okuma (smoke_sensor.h)
  □ L298N fan PWM kontrolü (fan_control.h) — vize kodunu uyarla
  □ Eşik testi: temiz hava / hafif duman / yoğun duman
  □ SMOKE_ALERT TFT ekranı + sunucu sesli uyarısı
  □ /smoke_alert endpoint (Piper TTS ile Türkçe uyarı)

FAZA 4 — Multimodal + RAG (2-3 hafta)
  □ gemma3:4b Ollama kurulumu + görsel test
  □ ChromaDB kurulumu + PDF kılavuz yükleme
  □ Mobil web UI fotoğraf upload
  □ Session fotoğraf bağlama mekanizması

FAZA 5 — Entegrasyon ve Test (1-2 hafta)
  □ RFID → Session → AES key üretimi tam akışı
  □ TFT tüm state'ler + animasyonlar (SMOKE_ALERT dahil)
  □ Uçtan uca senaryo testi (normal kullanım + duman senaryosu)
  □ Güvenlik testi (AES, TLS doğrulama)
  □ MQ135 kalibrasyon testi (referans gaz veya sigara dumanı)
```

---

## 13. Teknik Özgün Değer Özeti

| Özellik | Açıklama |
|---------|----------|
| **Push-to-Talk** | Endüstriyel gürültüde sıfır yanlış tetiklenme |
| **Streaming Pipeline** | Cümle bazlı TTS ile ~1.5s algılanan gecikme |
| **Multimodal RAG** | Fotoğraf + ses + PDF kılavuz = hallucination yok |
| **AES-256 (2 katman)** | TLS + uygulama seviyesi, kurumsal gizlilik |
| **Offline çalışma** | Sıfır cloud bağımlılığı, internet kesintisine dayanıklı |
| **ILI9341 TFT** | Kullanıcıya anlık görsel geri bildirim + SMOKE_ALERT |
| **ESP32-S3 HW AES** | Şifreleme CPU yüksüz, gerçek zamanlı |
| **4GB VRAM optimize** | gemma3:4b Q4 — güçlü vision, erişilebilir donanım |
| **MQ135 + L298N Fan** | Duman tespitinde otomatik havalandırma + sesli uyarı |

---

*SmartLab Asistanı — IoT Final Projesi Taslağı v1.1*  
*Tarih: Mayıs 2026 — MQ135 duman sensörü + L298N fan sistemi eklendi*
