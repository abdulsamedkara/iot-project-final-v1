"""
main.py — SmartLab FastAPI WebSocket Sunucusu
Faza 1: WebSocket + AES-256 + STT + TTS

Başlatma:
  uvicorn main:app --host 0.0.0.0 --port 8080 --reload

WebSocket Protokolü:
  BAĞLANTI → Sunucu: {"type":"session","session_id":"...","key_b64":"..."}
  ESP32→Sunucu (binary): [0x01][IV 16B][AES-CBC(PCM)]
  ESP32→Sunucu (text):   {"type":"rfid","uid":"AABBCCDD","session_id":"..."}
  Sunucu→ESP32 (text):   {"type":"user","name":"..."}
  Sunucu→ESP32 (text):   {"type":"transcript","text":"..."}
  Sunucu→ESP32 (binary): [IV 16B][AES-CBC(PCM yanıt)]
"""

import json
import logging
import asyncio
import time

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.responses import Response

import crypto
import stt
import tts
from session import store

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("smartlab")

app = FastAPI(title="SmartLab Asistan API")

# ─── Basit LLM (Faza 1 — Faza 2'de Ollama ile değiştirilir) ─────────────────
def simple_llm(transcript: str, username: str) -> str:
    """
    Faza 1'de gerçek LLM yerine basit kural tabanlı yanıt.
    Faza 2'de bu fonksiyon Ollama VLM stream ile değiştirilecek.
    """
    greet = f"Merhaba {username}! " if username and username != "Misafir" else ""

    if not transcript.strip():
        return greet + "Sizi duyamadım, lütfen tekrar söyler misiniz?"

    low = transcript.lower()
    if any(w in low for w in ["merhaba", "selam", "hey"]):
        return greet + "Merhaba! SmartLab asistanınım. Nasıl yardımcı olabilirim?"
    elif any(w in low for w in ["nasıl", "nasil", "ne yapabilirim"]):
        return (greet + "Atölye cihazları hakkında sorularınızı yanıtlayabilirim. "
                "Cihazın fotoğrafını çekip sorunuzu sesli sorabilirsiniz.")
    elif any(w in low for w in ["test", "deneme", "çalışıyor", "calisıyor"]):
        return greet + "Evet, sistem çalışıyor. Ses iletişimi başarılı!"
    else:
        return (greet + f"'{transcript}' sorunuzu aldım. "
                "Faza 2'de AI motoru ile yanıt vereceğim.")


# ─── WebSocket endpoint ───────────────────────────────────────────────────────
@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()

    # Yeni session oluştur
    sess = store.create()
    log.info(f"[{sess.session_id[:8]}] Bağlantı kabul edildi")

    # Hemen session bilgisini gönder
    await ws.send_text(json.dumps({
        "type":       "session",
        "session_id": sess.session_id,
        "key_b64":    sess.key_b64,
    }))
    log.info(f"[{sess.session_id[:8]}] Session gönderildi")

    try:
        while True:
            message = await ws.receive()

            # ── Text frame (JSON kontrol mesajı) ──────────────────────────────
            if "text" in message:
                try:
                    data = json.loads(message["text"])
                    msg_type = data.get("type", "")

                    if msg_type == "rfid":
                        uid = data.get("uid", "").upper()
                        username = store.set_user(sess.session_id, uid)
                        log.info(f"[{sess.session_id[:8]}] RFID: {uid} → {username}")
                        await ws.send_text(json.dumps({
                            "type": "user",
                            "name": username,
                        }))

                except json.JSONDecodeError:
                    log.warning(f"[{sess.session_id[:8]}] JSON parse hatası")

            # ── Binary frame (ses verisi) ──────────────────────────────────────
            elif "bytes" in message:
                raw = message["bytes"]

                # Beklenen format: [0x01][IV 16B][AES-CBC(PCM)]
                if len(raw) < 18 or raw[0] != 0x01:
                    log.warning(f"[{sess.session_id[:8]}] Geçersiz binary frame")
                    continue

                encrypted_payload = raw[1:]  # Tip byte'ını çıkar
                t0 = time.time()

                # Deşifre
                try:
                    pcm_bytes = crypto.decrypt(sess.key_bytes, encrypted_payload)
                except Exception as e:
                    log.error(f"[{sess.session_id[:8]}] Deşifre hatası: {e}")
                    continue

                log.info(f"[{sess.session_id[:8]}] PCM alındı: {len(pcm_bytes)} byte "
                         f"({len(pcm_bytes)/16000/2:.2f}s) — deşifre: {time.time()-t0:.3f}s")

                # STT
                t1 = time.time()
                transcript = await asyncio.get_event_loop().run_in_executor(
                    None, stt.transcribe, pcm_bytes
                )
                log.info(f"[{sess.session_id[:8]}] STT ({time.time()-t1:.2f}s): '{transcript}'")

                # Transcript'i ESP32'ye gönder (ekranda gösterim için)
                await ws.send_text(json.dumps({
                    "type": "transcript",
                    "text": transcript,
                }))

                # LLM (Faza 1: basit kural tabanlı)
                t2 = time.time()
                current_sess = store.get(sess.session_id)
                username = current_sess.username if current_sess else "Misafir"
                answer = simple_llm(transcript, username)
                log.info(f"[{sess.session_id[:8]}] LLM ({time.time()-t2:.2f}s): '{answer}'")

                # TTS
                t3 = time.time()
                audio_pcm = await asyncio.get_event_loop().run_in_executor(
                    None, tts.synthesize, answer
                )
                log.info(f"[{sess.session_id[:8]}] TTS ({time.time()-t3:.2f}s): "
                         f"{len(audio_pcm)} byte PCM")

                if not audio_pcm:
                    log.error(f"[{sess.session_id[:8]}] TTS boş çıktı")
                    continue

                # Şifrele ve gönder: IV(16) | AES-CBC(PCM)
                encrypted_audio = crypto.encrypt(sess.key_bytes, audio_pcm)
                await ws.send_bytes(encrypted_audio)

                total_ms = (time.time() - t0) * 1000
                log.info(f"[{sess.session_id[:8]}] ✓ Toplam: {total_ms:.0f}ms")

    except WebSocketDisconnect:
        log.info(f"[{sess.session_id[:8]}] Bağlantı kesildi")
    except Exception as e:
        log.error(f"[{sess.session_id[:8]}] Beklenmeyen hata: {e}", exc_info=True)
    finally:
        store.remove(sess.session_id)
        log.info(f"[{sess.session_id[:8]}] Session temizlendi")


# ─── Duman Sensörü Uyarı Endpoint'i (Faza 3'e hazır) ─────────────────────────
@app.post("/smoke_alert")
async def smoke_alert(req: Request):
    """
    ESP32'den gelen duman uyarısı.
    MQ135 ADC değerine göre Türkçe sesli uyarı üretir.
    """
    body = await req.json()
    adc_val = body.get("adc_value", 0)
    session_id = body.get("session_id", "")

    current_sess = store.get(session_id)
    username = current_sess.username if current_sess else ""

    if adc_val >= 2000:
        text = (f"Dikkat{', ' + username if username else ''}! "
                "Laboratuvarda yoğun duman tespit edildi. "
                "Lütfen çalışma alanını derhal terk edin ve pencereyi açın.")
    else:
        text = (f"Uyarı{', ' + username if username else ''}. "
                "Hafif gaz veya duman algılandı. "
                "Havalandırma sistemi devreye girdi.")

    log.warning(f"[DUMAN] ADC={adc_val} → '{text}'")

    audio_pcm = await asyncio.get_event_loop().run_in_executor(
        None, tts.synthesize, text
    )

    if not audio_pcm:
        return Response(status_code=204)

    # Şifrelenmiş yanıt (session anahtarıyla)
    if current_sess:
        encrypted = crypto.encrypt(current_sess.key_bytes, audio_pcm)
        return Response(content=encrypted, media_type="application/octet-stream")
    else:
        # Session yoksa ham PCM döndür (uyarı yine de çıksın)
        return Response(content=audio_pcm, media_type="application/octet-stream")


# ─── Health check ─────────────────────────────────────────────────────────────
@app.get("/health")
async def health():
    return {"status": "ok", "sessions": len(store._sessions)}
