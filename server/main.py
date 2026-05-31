"""
main.py — SmartLab FastAPI WebSocket Sunucusu (Faza 1-4)

Başlatma:
  uvicorn main:app --host 0.0.0.0 --port 8080

WebSocket Protokolü:
  BAĞLANTI → Sunucu: {"type":"session","session_id":"...","key_b64":"..."}
  ESP32→Sunucu (binary): [0x01][IV 16B][AES-CBC(PCM)]
  ESP32→Sunucu (text):   {"type":"rfid","uid":"AABBCCDD","session_id":"..."}
  Sunucu→ESP32 (text):   {"type":"user","name":"..."}
  Sunucu→ESP32 (text):   {"type":"transcript","text":"..."}
  Sunucu→ESP32 (binary): [IV 16B][AES-CBC(PCM yanıt)]

REST:
  POST /upload/{session_id}              — fotoğraf yükle (multipart, field: file)
  POST /api/session/{session_id}/photo   — fotoğraf yükle (multipart, field: photo)
  GET  /api/rfid/pending                 — web UI RFID poll (consume one event)
  WS   /ws/rfid                          — web UI RFID push stream
  POST /smoke_alert                      — ESP32 duman uyarısı
  GET  /sessions                         — aktif session listesi
  GET  /health                           — durum kontrolü
"""

import asyncio
import base64
import json
import logging
import time
import threading
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request, UploadFile, File
from fastapi.responses import Response, JSONResponse, FileResponse
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware

import crypto
import stt
import tts
import llm
import rag
from session import store

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("smartlab")

# ─── RFID event broadcast ─────────────────────────────────────────────────────
# Pending events for GET /api/rfid/pending (polling fallback)
_rfid_events: list[dict] = []
_rfid_events_lock = threading.Lock()

# Connected /ws/rfid WebSocket queues (push stream)
_rfid_ws_queues: list[asyncio.Queue] = []
_rfid_ws_queues_lock = asyncio.Lock()


async def _broadcast_rfid(event: dict):
    """Push RFID event to polling list and all connected /ws/rfid clients."""
    event["_ts"] = time.time()   # timestamp for expiry check
    with _rfid_events_lock:
        _rfid_events.append(event)
        if len(_rfid_events) > 20:
            _rfid_events.pop(0)

    async with _rfid_ws_queues_lock:
        for q in list(_rfid_ws_queues):
            try:
                q.put_nowait(event)
            except asyncio.QueueFull:
                pass


@asynccontextmanager
async def lifespan(app: FastAPI):
    log.info("RAG: PDF indeksleme başlıyor...")
    try:
        added = await asyncio.get_event_loop().run_in_executor(None, rag.index_pdfs)
        log.info(f"RAG: {added} chunk indekslendi.")
    except Exception as e:
        log.warning(f"RAG indeksleme atlandı: {e}")
    yield
    log.info("Sunucu kapatılıyor.")


app = FastAPI(title="SmartLab Asistan API", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


# ─── Main ESP32 WebSocket ────────────────────────────────────────────────────
@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()

    sess = store.create()
    log.info(f"[{sess.session_id[:8]}] Bağlantı kabul edildi")

    await ws.send_text(json.dumps({
        "type":       "session",
        "session_id": sess.session_id,
        "key_b64":    sess.key_b64,
    }, separators=(',', ':')))

    try:
        while True:
            message = await ws.receive()

            if message.get("type") == "websocket.disconnect":
                raise WebSocketDisconnect(code=message.get("code", 1000))

            if "text" in message:
                try:
                    data = json.loads(message["text"])
                    msg_type = data.get("type", "")

                    if msg_type == "sensors":
                        current_sess = store.get(sess.session_id)
                        if current_sess:
                            current_sess.sensors = {
                                "temperature": data.get("temperature"),
                                "humidity":    data.get("humidity"),
                                "smoke":       data.get("smoke"),
                                "ldr":         data.get("ldr"),
                                "pir":         data.get("pir"),
                                "flame":       data.get("flame"),
                                "vib":         data.get("vib"),
                                "ts":          time.time(),
                            }

                    elif msg_type == "rfid":
                        uid = data.get("uid", "").upper()
                        username = store.set_user(sess.session_id, uid)
                        log.info(f"[{sess.session_id[:8]}] RFID: {uid} → {username}")

                        await ws.send_text(json.dumps({
                            "type": "user",
                            "name": username,
                        }, separators=(',', ':')))

                        # Notify web UI clients
                        await _broadcast_rfid({
                            "event":      "card_detected",
                            "session_id": sess.session_id,
                            "user_name":  username,
                            "uid":        uid,
                        })

                except json.JSONDecodeError:
                    log.warning(f"[{sess.session_id[:8]}] JSON parse hatası")

            elif "bytes" in message:
                raw = message["bytes"]

                if len(raw) < 18 or raw[0] != 0x01:
                    log.warning(f"[{sess.session_id[:8]}] Geçersiz binary frame")
                    continue

                t0 = time.time()

                try:
                    pcm_bytes = crypto.decrypt(sess.key_bytes, raw[1:])
                except Exception as e:
                    log.error(f"[{sess.session_id[:8]}] Deşifre hatası: {e}")
                    continue

                log.info(f"[{sess.session_id[:8]}] PCM: {len(pcm_bytes)//2/16000:.2f}s")

                t1 = time.time()
                transcript = await asyncio.get_event_loop().run_in_executor(
                    None, stt.transcribe, pcm_bytes
                )
                log.info(f"[{sess.session_id[:8]}] STT ({time.time()-t1:.2f}s): '{transcript}'")

                await ws.send_text(json.dumps({
                    "type": "transcript",
                    "text": transcript,
                }, separators=(',', ':')))

                rag_ctx = await asyncio.get_event_loop().run_in_executor(
                    None, rag.query, transcript
                )

                current_sess = store.get(sess.session_id)
                username  = current_sess.username  if current_sess else "Misafir"
                image_b64 = current_sess.image_b64 if current_sess else None

                t2 = time.time()
                answer = await llm.generate(
                    transcript=transcript,
                    username=username,
                    image_b64=image_b64,
                    rag_context=rag_ctx,
                )
                log.info(f"[{sess.session_id[:8]}] LLM ({time.time()-t2:.2f}s): '{answer[:60]}'")

                # Chat geçmişine kaydet
                current_sess2 = store.get(sess.session_id)
                if current_sess2:
                    now_ts = time.time()
                    current_sess2.messages.append({"role": "user",      "text": transcript, "ts": now_ts})
                    current_sess2.messages.append({"role": "assistant", "text": answer,     "ts": now_ts})
                    if len(current_sess2.messages) > 40:
                        current_sess2.messages = current_sess2.messages[-40:]

                t3 = time.time()
                audio_pcm = await asyncio.get_event_loop().run_in_executor(
                    None, tts.synthesize, answer
                )
                log.info(f"[{sess.session_id[:8]}] TTS ({time.time()-t3:.2f}s): "
                         f"{len(audio_pcm)} byte")

                if not audio_pcm:
                    log.error(f"[{sess.session_id[:8]}] TTS boş çıktı")
                    continue

                encrypted_audio = crypto.encrypt(sess.key_bytes, audio_pcm)
                await ws.send_bytes(encrypted_audio)

                log.info(f"[{sess.session_id[:8]}] ✓ Toplam: {(time.time()-t0)*1000:.0f}ms")

    except WebSocketDisconnect:
        log.info(f"[{sess.session_id[:8]}] Bağlantı kesildi")
    except Exception as e:
        log.error(f"[{sess.session_id[:8]}] Beklenmeyen hata: {e}", exc_info=True)
    finally:
        store.remove(sess.session_id)


# ─── Web UI: RFID push stream ─────────────────────────────────────────────────
@app.websocket("/ws/rfid")
async def ws_rfid(ws: WebSocket):
    """Pushes RFID card_detected events to connected web UI clients."""
    await ws.accept()
    q: asyncio.Queue = asyncio.Queue(maxsize=10)

    async with _rfid_ws_queues_lock:
        _rfid_ws_queues.append(q)

    log.info("WS /ws/rfid: web UI bağlandı")
    try:
        while True:
            try:
                event = await asyncio.wait_for(q.get(), timeout=25.0)
                await ws.send_text(json.dumps(event))
            except asyncio.TimeoutError:
                # Keep-alive ping
                await ws.send_text(json.dumps({"event": "ping"}))
    except WebSocketDisconnect:
        pass
    except Exception as e:
        log.debug(f"WS /ws/rfid kapatıldı: {e}")
    finally:
        async with _rfid_ws_queues_lock:
            try:
                _rfid_ws_queues.remove(q)
            except ValueError:
                pass
        log.info("WS /ws/rfid: web UI ayrıldı")


# ─── Web UI: RFID polling fallback ───────────────────────────────────────────
_RFID_EVENT_TTL = 30.0   # saniye — daha eski eventler stale sayılır

@app.get("/api/rfid/pending")
async def rfid_pending():
    """
    Returns and removes the oldest pending RFID event (max 10s old).
    Returns 204 when no fresh events are queued.
    """
    now = time.time()
    with _rfid_events_lock:
        # Süresi dolmuş eventleri temizle
        while _rfid_events and now - _rfid_events[0].get("_ts", 0) > _RFID_EVENT_TTL:
            stale = _rfid_events.pop(0)
            log.debug(f"Stale RFID event temizlendi: {stale.get('uid')}")
        if _rfid_events:
            event = _rfid_events.pop(0)
            event.pop("_ts", None)   # iç alan web UI'a gitmesin
            return JSONResponse(event)
    return Response(status_code=204)


# ─── Demo session (web UI demo button) ───────────────────────────────────────
@app.post("/api/demo/session")
async def demo_session():
    """Creates a real session with a demo user — used by the web UI demo button."""
    sess = store.create()
    with store._lock:
        sess.username = "Demo Kullanıcı"
    event = {
        "event":      "card_detected",
        "session_id": sess.session_id,
        "user_name":  sess.username,
        "uid":        "DEMO0000",
    }
    await _broadcast_rfid(event)
    log.info(f"[{sess.session_id[:8]}] Demo session oluşturuldu")
    return {"session_id": sess.session_id, "user_name": sess.username}


# ─── Photo upload (original route, field: file) ───────────────────────────────
@app.post("/upload/{session_id}")
async def upload_image(session_id: str, file: UploadFile = File(...)):
    return await _handle_upload(session_id, file)


# ─── Photo upload alias (design route, field: photo) ─────────────────────────
@app.post("/api/session/{session_id}/photo")
async def upload_photo_alias(session_id: str, photo: UploadFile = File(...)):
    return await _handle_upload(session_id, photo)


def _resize_image(data: bytes, max_px: int = 800, quality: int = 75) -> bytes:
    """Resize image to max_px on longest side, re-encode as JPEG to reduce size."""
    try:
        from PIL import Image
        import io
        img = Image.open(io.BytesIO(data))
        if img.mode not in ("RGB", "L"):
            img = img.convert("RGB")
        w, h = img.size
        if max(w, h) > max_px:
            scale = max_px / max(w, h)
            img = img.resize((int(w * scale), int(h * scale)), Image.LANCZOS)
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=quality, optimize=True)
        resized = buf.getvalue()
        log.info(f"Image resize: {len(data)//1024}KB → {len(resized)//1024}KB "
                 f"({img.size[0]}×{img.size[1]})")
        return resized
    except Exception as e:
        log.warning(f"Image resize failed ({e}), using original")
        return data


async def _handle_upload(session_id: str, upload: UploadFile) -> JSONResponse | Response:
    sess = store.get(session_id)
    if not sess:
        return JSONResponse({"error": "Session bulunamadı"}, status_code=404)

    content = await upload.read()
    if len(content) > 20 * 1024 * 1024:
        return JSONResponse({"error": "Dosya çok büyük (max 20MB)"}, status_code=413)

    # Resize before storing — large images make vision LLM very slow
    content = await asyncio.get_event_loop().run_in_executor(
        None, _resize_image, content
    )

    image_b64 = base64.b64encode(content).decode()
    ok = store.set_image(session_id, image_b64)
    if not ok:
        return JSONResponse({"error": "Session güncellenemedi"}, status_code=500)

    log.info(f"[{session_id[:8]}] Fotoğraf alındı: {len(content)//1024}KB "
             f"({upload.content_type})")
    return JSONResponse({"status": "ok", "size_kb": len(content) // 1024})


# ─── Smoke alert ─────────────────────────────────────────────────────────────
@app.post("/smoke_alert")
async def smoke_alert(req: Request):
    body = await req.json()
    adc_val    = body.get("adc_value", 0)
    session_id = body.get("session_id", "")

    current_sess = store.get(session_id)
    username = current_sess.username if current_sess else ""

    if adc_val >= 2000:
        text = (
            f"Warning{', ' + username if username else ''}! "
            "High smoke concentration detected in the lab. "
            "Please evacuate immediately and open the windows."
        )
    else:
        text = (
            f"Caution{', ' + username if username else ''}. "
            "Mild gas or smoke detected. "
            "Ventilation system has been activated."
        )

    log.warning(f"[SMOKE] ADC={adc_val} → '{text}'")

    audio_pcm = await asyncio.get_event_loop().run_in_executor(
        None, tts.synthesize, text
    )

    if not audio_pcm:
        return Response(status_code=204)

    if current_sess:
        encrypted = crypto.encrypt(current_sess.key_bytes, audio_pcm)
        return Response(content=encrypted, media_type="application/octet-stream")
    return Response(content=audio_pcm, media_type="application/octet-stream")


# ─── Session data (chat + sensors) ──────────────────────────────────────────
@app.get("/api/session/{session_id}/data")
async def session_data(session_id: str):
    sess = store.get(session_id)
    if not sess:
        return JSONResponse({"error": "Session bulunamadı"}, status_code=404)
    return {
        "messages":  sess.messages,
        "sensors":   sess.sensors,
        "has_photo": sess.image_b64 is not None,
        "username":  sess.username,
    }


# ─── Sessions list ────────────────────────────────────────────────────────────
@app.get("/sessions")
async def list_sessions():
    now = time.time()
    with store._lock:
        sessions = [
            {
                "session_id": sid,
                "username":   s.username,
                "has_image":  s.image_b64 is not None,
                "age_sec":    int(now - s.created_at),
                "rfid_scanned": s.rfid_uid is not None,
            }
            for sid, s in store._sessions.items()
        ]
    return {"sessions": sessions}


# ─── Health check ─────────────────────────────────────────────────────────────
@app.get("/health")
async def health():
    return {
        "status":   "ok",
        "sessions": len(store._sessions),
        "rag_docs": rag._collection.count() if rag._collection else 0,
    }


# ─── Serve web UI (must be last — catches everything else) ────────────────────
_WEB_UI_DIR = Path(__file__).parent.parent / "web_ui"

if _WEB_UI_DIR.exists():
    app.mount("/", StaticFiles(directory=str(_WEB_UI_DIR), html=True), name="web_ui")
