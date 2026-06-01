"""
main.py

SmartLab FastAPI WebSocket Server (Phase 1-4)

Start Command:
  uvicorn main:app --host 0.0.0.0 --port 8080

WebSocket Protocol:
  CONNECTION -> Server: {"type":"session","session_id":"...","key_b64":"..."}
  ESP32->Server (binary): [0x01][IV 16B][AES-CBC(PCM)]
  ESP32->Server (text):   {"type":"rfid","uid":"AABBCCDD","session_id":"..."}
  Server->ESP32 (text):   {"type":"user","name":"..."}
  Server->ESP32 (text):   {"type":"transcript","text":"..."}
  Server->ESP32 (binary): [IV 16B][AES-CBC(PCM response)]

REST Endpoints:
  POST /upload/{session_id}              - Uploads a photo (multipart, field: file)
  POST /api/session/{session_id}/photo   - Uploads a photo (multipart, field: photo)
  GET  /api/rfid/pending                 - Web UI RFID polling endpoint (consumes one event)
  WS   /ws/rfid                          - Web UI RFID push stream
  POST /smoke_alert                      - Handles smoke alert from ESP32
  GET  /sessions                         - Returns a list of active sessions
  GET  /health                           - Server health check endpoint
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

# Configure standard logging format and level
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("smartlab")

# Dictionary holding command queues to send commands from the server to specific ESP32 sessions
_esp32_cmd_queues: dict[str, asyncio.Queue] = {}
_esp32_cmd_lock = asyncio.Lock()

async def _send_to_esp32(session_id: str, payload: dict) -> bool:
    """
    Places a command payload in the queue for a specific ESP32 session.
    Returns True if successfully queued, False otherwise.
    """
    async with _esp32_cmd_lock:
        q = _esp32_cmd_queues.get(session_id)
    if q:
        try:
            q.put_nowait(payload)
            return True
        except asyncio.QueueFull:
            pass
    return False


# A list of queues used to push live sensor updates to connected Web UI clients
_sensor_ws_queues: list[asyncio.Queue] = []
_sensor_ws_queues_lock = asyncio.Lock()

async def _push_sensors(payload: dict):
    """
    Broadcasts incoming sensor data to all connected Web UI WebSocket clients.
    """
    async with _sensor_ws_queues_lock:
        for q in list(_sensor_ws_queues):
            try:
                q.put_nowait(payload)
            except asyncio.QueueFull:
                pass


# List of pending RFID events for the HTTP polling fallback method
_rfid_events: list[dict] = []
_rfid_events_lock = threading.Lock()

# List of WebSocket queues to push real-time RFID detection events
_rfid_ws_queues: list[asyncio.Queue] = []
_rfid_ws_queues_lock = asyncio.Lock()


async def _broadcast_rfid(event: dict):
    """
    Dispatches a new RFID scan event.
    It appends the event to a polling list (for GET requests) and broadcasts
    it directly to all active /ws/rfid WebSocket clients.
    """
    # Attach a timestamp for expiration checks later
    event["_ts"] = time.time()   
    
    with _rfid_events_lock:
        _rfid_events.append(event)
        # Keep a maximum history of 20 events
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
    """
    Manages the FastAPI application lifecycle.
    On startup, it triggers the indexing of PDF documents into the RAG system.
    """
    log.info("RAG: PDF indexing started...")
    try:
        added = await asyncio.get_event_loop().run_in_executor(None, rag.index_pdfs)
        log.info(f"RAG: {added} chunks indexed.")
    except Exception as e:
        log.warning(f"RAG indexing skipped: {e}")
        
    yield
    log.info("Server shutting down.")


app = FastAPI(title="SmartLab Assistant API", lifespan=lifespan)

# Allow Cross-Origin Resource Sharing (CORS) for external web interfaces
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    """
    Main WebSocket endpoint for the ESP32 microcontroller.
    Handles incoming sensor data, RFID scans, and encrypted audio streams.
    """
    await ws.accept()

    # Create a new session and generate security keys
    sess = store.create()
    log.info(f"[{sess.session_id[:8]}] Connection accepted")

    # Send the newly generated session keys to the ESP32 to establish encryption
    await ws.send_text(json.dumps({
        "type":       "session",
        "session_id": sess.session_id,
        "key_b64":    sess.key_b64,
    }, separators=(',', ':')))

    # Register an outgoing command queue specifically for this ESP32 session
    cmd_q: asyncio.Queue = asyncio.Queue(maxsize=20)
    async with _esp32_cmd_lock:
        _esp32_cmd_queues[sess.session_id] = cmd_q

    try:
        while True:
            # First, check and dispatch any outgoing commands to the ESP32 without blocking
            while not cmd_q.empty():
                try:
                    cmd = cmd_q.get_nowait()
                    await ws.send_text(json.dumps(cmd, separators=(',', ':')))
                except asyncio.QueueEmpty:
                    break

            # Wait briefly for incoming messages from the ESP32
            try:
                message = await asyncio.wait_for(ws.receive(), timeout=0.1)
            except asyncio.TimeoutError:
                continue

            # Handle explicit disconnection messages
            if message.get("type") == "websocket.disconnect":
                raise WebSocketDisconnect(code=message.get("code", 1000))

            # Handle JSON-formatted text messages (sensors, RFID)
            if "text" in message:
                try:
                    data = json.loads(message["text"])
                    msg_type = data.get("type", "")

                    if msg_type == "sensors":
                        current_sess = store.get(sess.session_id)
                        if current_sess:
                            now = time.time()
                            prev = current_sess.sensors
                            
                            # Preserve timestamps for transient sensor events
                            vib_last   = now if data.get("vib")   == 1 else prev.get("vib_last_ts", 0)
                            pir_last   = now if data.get("pir")   == 1 else prev.get("pir_last_ts", 0)
                            flame_last = now if data.get("flame") == 0 else prev.get("flame_last_ts", 0)
                            
                            sensor_payload = {
                                "temperature":   data.get("temperature"),
                                "humidity":      data.get("humidity"),
                                "smoke":         data.get("smoke"),
                                "ldr":           data.get("ldr"),
                                "pir":           data.get("pir"),
                                "flame":         data.get("flame"),
                                "vib":           data.get("vib"),
                                "ts":            now,
                                "vib_last_ts":   vib_last,
                                "pir_last_ts":   pir_last,
                                "flame_last_ts": flame_last,
                                "session_id":    sess.session_id,
                            }
                            
                            current_sess.sensors = sensor_payload
                            await _push_sensors(sensor_payload)

                    elif msg_type == "rfid":
                        # Process RFID scan events and map the UID to a username
                        uid = data.get("uid", "").upper()
                        username = store.set_user(sess.session_id, uid)
                        log.info(f"[{sess.session_id[:8]}] RFID scan: {uid} maps to {username}")

                        # Inform the ESP32 about the identified user
                        await ws.send_text(json.dumps({
                            "type": "user",
                            "name": username,
                        }, separators=(',', ':')))

                        # Broadcast the detection event to the Web UI
                        await _broadcast_rfid({
                            "event":      "card_detected",
                            "session_id": sess.session_id,
                            "user_name":  username,
                            "uid":        uid,
                        })

                except json.JSONDecodeError:
                    log.warning(f"[{sess.session_id[:8]}] Failed to parse incoming JSON")

            # Handle encrypted binary audio frames (microphone data from ESP32)
            elif "bytes" in message:
                raw = message["bytes"]

                # Validate the binary frame header structure
                if len(raw) < 18 or raw[0] != 0x01:
                    log.warning(f"[{sess.session_id[:8]}] Received an invalid binary frame")
                    continue

                t0 = time.time()

                # Decrypt the audio data
                try:
                    pcm_bytes = crypto.decrypt(sess.key_bytes, raw[1:])
                except Exception as e:
                    log.error(f"[{sess.session_id[:8]}] Audio decryption failed: {e}")
                    continue

                log.info(f"[{sess.session_id[:8]}] Decrypted PCM audio: {len(pcm_bytes)//2/16000:.2f} seconds")

                # Verify that a user has scanned their RFID card before processing audio queries
                current_sess = store.get(sess.session_id)
                if not current_sess or not current_sess.rfid_uid:
                    log.warning(f"[{sess.session_id[:8]}] Interaction blocked: No active RFID scan.")
                    text = "Please scan your ID card first."
                    audio_pcm = await asyncio.get_event_loop().run_in_executor(
                        None, tts.synthesize, text
                    )
                    if audio_pcm:
                        encrypted_audio = crypto.encrypt(sess.key_bytes, audio_pcm)
                        await ws.send_bytes(encrypted_audio)
                    continue

                # Process the audio with Speech-to-Text
                t1 = time.time()
                transcript = await asyncio.get_event_loop().run_in_executor(
                    None, stt.transcribe, pcm_bytes
                )
                log.info(f"[{sess.session_id[:8]}] STT processed in {time.time()-t1:.2f}s: '{transcript}'")

                # Echo the transcribed text back to the ESP32
                await ws.send_text(json.dumps({
                    "type": "transcript",
                    "text": transcript,
                }, separators=(',', ':')))

                # Retrieve relevant context from the PDF knowledge base
                rag_ctx = await asyncio.get_event_loop().run_in_executor(
                    None, rag.query, transcript
                )

                current_sess = store.get(sess.session_id)
                username  = current_sess.username  if current_sess else "Guest"
                raw_image = current_sess.image_b64 if current_sess else None

                # Supply the image to the LLM only if the user explicitly mentions words like "image" or "photo"
                low_t = transcript.lower()
                image_b64 = raw_image if raw_image and ("image" in low_t or "photo" in low_t) else None
                if raw_image and not image_b64:
                    log.info(f"[{sess.session_id[:8]}] Image provided but skipped as no trigger words were detected")

                # Generate the AI response using the local Ollama model
                t2 = time.time()
                answer = await llm.generate(
                    transcript=transcript,
                    username=username,
                    image_b64=image_b64,
                    rag_context=rag_ctx,
                )
                log.info(f"[{sess.session_id[:8]}] LLM completed in {time.time()-t2:.2f}s: '{answer[:60]}'")

                # Store the conversation history for Web UI display
                current_sess2 = store.get(sess.session_id)
                if current_sess2:
                    now_ts = time.time()
                    current_sess2.messages.append({"role": "user",      "text": transcript, "ts": now_ts})
                    current_sess2.messages.append({"role": "assistant", "text": answer,     "ts": now_ts})
                    # Keep a maximum of 40 messages to conserve memory
                    if len(current_sess2.messages) > 40:
                        current_sess2.messages = current_sess2.messages[-40:]

                # Convert the AI response text into synthesized audio
                t3 = time.time()
                audio_pcm = await asyncio.get_event_loop().run_in_executor(
                    None, tts.synthesize, answer
                )
                log.info(f"[{sess.session_id[:8]}] TTS completed in {time.time()-t3:.2f}s: {len(audio_pcm)} bytes generated")

                if not audio_pcm:
                    log.error(f"[{sess.session_id[:8]}] TTS output is completely empty")
                    continue

                # Encrypt the generated audio and send it back to the ESP32
                encrypted_audio = crypto.encrypt(sess.key_bytes, audio_pcm)
                await ws.send_bytes(encrypted_audio)

                log.info(f"[{sess.session_id[:8]}] Interaction successfully completed in {(time.time()-t0)*1000:.0f}ms")

    except WebSocketDisconnect:
        log.info(f"[{sess.session_id[:8]}] WebSocket connection closed by client")
    except Exception as e:
        log.error(f"[{sess.session_id[:8]}] An unexpected error occurred: {e}", exc_info=True)
    finally:
        # Clean up the session state when the connection drops
        store.remove(sess.session_id)
        async with _esp32_cmd_lock:
            _esp32_cmd_queues.pop(sess.session_id, None)


@app.websocket("/ws/rfid")
async def ws_rfid(ws: WebSocket):
    """
    WebSocket endpoint that pushes real-time RFID detection events 
    to connected Web UI clients.
    """
    await ws.accept()
    q: asyncio.Queue = asyncio.Queue(maxsize=10)

    async with _rfid_ws_queues_lock:
        _rfid_ws_queues.append(q)

    log.info("WebSocket /ws/rfid: Web UI connected")
    try:
        while True:
            try:
                # Wait for an RFID event, otherwise send a ping to keep connection alive
                event = await asyncio.wait_for(q.get(), timeout=25.0)
                await ws.send_text(json.dumps(event))
            except asyncio.TimeoutError:
                await ws.send_text(json.dumps({"event": "ping"}))
    except WebSocketDisconnect:
        pass
    except Exception as e:
        log.debug(f"WebSocket /ws/rfid encountered an issue: {e}")
    finally:
        async with _rfid_ws_queues_lock:
            try:
                _rfid_ws_queues.remove(q)
            except ValueError:
                pass
        log.info("WebSocket /ws/rfid: Web UI disconnected")


@app.websocket("/ws/sensors")
async def ws_sensors(ws: WebSocket):
    """
    WebSocket endpoint that streams live sensor telemetry 
    to connected Web UI dashboards.
    """
    await ws.accept()
    q: asyncio.Queue = asyncio.Queue(maxsize=20)
    async with _sensor_ws_queues_lock:
        _sensor_ws_queues.append(q)
    try:
        while True:
            try:
                payload = await asyncio.wait_for(q.get(), timeout=20.0)
                await ws.send_text(json.dumps(payload))
            except asyncio.TimeoutError:
                # Send periodic pings to avoid connection timeouts
                await ws.send_text(json.dumps({"event": "ping"}))
    except WebSocketDisconnect:
        pass
    except Exception as e:
        log.debug(f"WebSocket /ws/sensors closed unexpectedly: {e}")
    finally:
        async with _sensor_ws_queues_lock:
            try:
                _sensor_ws_queues.remove(q)
            except ValueError:
                pass


_RFID_EVENT_TTL = 30.0   # Events older than 30 seconds are considered stale

@app.get("/api/rfid/pending")
async def rfid_pending():
    """
    A polling fallback endpoint for the Web UI.
    It returns and consumes the oldest pending RFID event.
    Returns HTTP 204 (No Content) if no recent events are queued.
    """
    now = time.time()
    with _rfid_events_lock:
        # Prune expired events from the queue
        while _rfid_events and now - _rfid_events[0].get("_ts", 0) > _RFID_EVENT_TTL:
            stale = _rfid_events.pop(0)
            log.debug(f"Cleared stale RFID event from queue: {stale.get('uid')}")
            
        if _rfid_events:
            event = _rfid_events.pop(0)
            # Remove internal tracking timestamp before sending
            event.pop("_ts", None)   
            return JSONResponse(event)
            
    return Response(status_code=204)


@app.post("/api/demo/session")
async def demo_session():
    """
    Creates a simulated active session with a demo user.
    This functionality is primarily used by the Web UI demo button.
    """
    sess = store.create()
    with store._lock:
        sess.username = "Demo User"
        
    event = {
        "event":      "card_detected",
        "session_id": sess.session_id,
        "user_name":  sess.username,
        "uid":        "DEMO0000",
    }
    
    await _broadcast_rfid(event)
    log.info(f"[{sess.session_id[:8]}] A demo session was successfully created")
    return {"session_id": sess.session_id, "user_name": sess.username}


@app.post("/upload/{session_id}")
async def upload_image(session_id: str, file: UploadFile = File(...)):
    """
    Original endpoint for photo uploads, expecting a multipart form with a 'file' field.
    """
    return await _handle_upload(session_id, file)


@app.post("/api/session/{session_id}/photo")
async def upload_photo_alias(session_id: str, photo: UploadFile = File(...)):
    """
    Alias endpoint for photo uploads, expecting a multipart form with a 'photo' field.
    """
    return await _handle_upload(session_id, photo)


@app.post("/api/fan")
async def fan_control(req: Request):
    """
    API endpoint to control the lab ventilation fan from the Web UI.
    Expected JSON Body: {"on": true/false, "speed": 0-100, "mode": "manual"/"auto", "session_id": "..."}
    """
    body = await req.json()
    session_id = body.get("session_id", "")

    # If no session ID is provided, try to find an active session with an authenticated user
    if not session_id:
        with store._lock:
            for sid, s in store._sessions.items():
                if s.rfid_uid:
                    session_id = sid
                    break

    if not session_id:
        return JSONResponse({"error": "No active session found"}, status_code=404)

    cmd = {
        "type":  "fan",
        "on":    body.get("on", False),
        "speed": body.get("speed", 0),
        "mode":  body.get("mode", "manual"),
    }
    
    # Send the command asynchronously to the specific ESP32 session
    sent = await _send_to_esp32(session_id, cmd)
    log.info(f"Dispatched fan command -> [{session_id[:8]}] {cmd}. Success: {sent}")
    return {"ok": sent, "session_id": session_id}


@app.post("/api/led")
async def led_control(req: Request):
    """
    API endpoint to control the LED strip from the Web UI.
    Expected JSON Body: {"on": true/false, "brightness": 0-100, "session_id": "..."}
    """
    body = await req.json()
    session_id = body.get("session_id", "")

    # Automatically map to an active session if one isn't explicitly defined
    if not session_id:
        with store._lock:
            for sid, s in store._sessions.items():
                if s.rfid_uid:
                    session_id = sid
                    break

    if not session_id:
        return JSONResponse({"error": "No active session found"}, status_code=404)

    cmd = {
        "type":       "led",
        "on":         body.get("on", False),
        "brightness": body.get("brightness", 100),
    }
    
    sent = await _send_to_esp32(session_id, cmd)
    log.info(f"Dispatched LED command -> [{session_id[:8]}] {cmd}. Success: {sent}")
    return {"ok": sent, "session_id": session_id}


@app.post("/api/session/{session_id}/logout")
async def session_logout(session_id: str):
    """
    Endpoint triggered by Web UI logout.
    Sends a logout directive to the ESP32 and clears local session user variables.
    """
    await _send_to_esp32(session_id, {"type": "logout"})
    store.clear_user(session_id)
    log.info(f"[{session_id[:8]}] Logout processed. User data cleared, but WebSocket remains open.")
    return {"ok": True}


def _resize_image(data: bytes, max_px: int = 512, quality: int = 70) -> bytes:
    """
    Resizes an uploaded image so that its longest side is no larger than max_px.
    It re-encodes the result as a JPEG to significantly reduce payload size,
    thereby improving Vision LLM processing speed.
    """
    try:
        from PIL import Image
        import io
        img = Image.open(io.BytesIO(data))
        
        # Ensure image is in a standard color format
        if img.mode not in ("RGB", "L"):
            img = img.convert("RGB")
            
        w, h = img.size
        if max(w, h) > max_px:
            scale = max_px / max(w, h)
            # Use LANCZOS resampling for high-quality downscaling
            img = img.resize((int(w * scale), int(h * scale)), Image.LANCZOS)
            
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=quality, optimize=True)
        resized = buf.getvalue()
        
        log.info(f"Image resized successfully: {len(data)//1024}KB to {len(resized)//1024}KB "
                 f"({img.size[0]}x{img.size[1]})")
        return resized
    except Exception as e:
        log.warning(f"Image resizing failed due to an error ({e}), utilizing the original uploaded file instead")
        return data


async def _handle_upload(session_id: str, upload: UploadFile) -> JSONResponse | Response:
    """
    Handles internal image upload logic, applying size validations and resizing optimizations.
    Saves the final base64 representation to the session.
    """
    sess = store.get(session_id)
    if not sess:
        return JSONResponse({"error": "Associated session not found"}, status_code=404)

    content = await upload.read()
    
    # Enforce a strict 20MB upper limit on uploads
    if len(content) > 20 * 1024 * 1024:
        return JSONResponse({"error": "The uploaded file is too large. Maximum size is 20MB"}, status_code=413)

    # Perform resizing in a separate thread to avoid blocking the async event loop
    content = await asyncio.get_event_loop().run_in_executor(
        None, _resize_image, content
    )

    image_b64 = base64.b64encode(content).decode()
    ok = store.set_image(session_id, image_b64)
    if not ok:
        return JSONResponse({"error": "Failed to attach image to session"}, status_code=500)

    log.info(f"[{session_id[:8]}] Photo upload received and processed: {len(content)//1024}KB "
             f"({upload.content_type})")
    return JSONResponse({"status": "ok", "size_kb": len(content) // 1024})


@app.post("/smoke_alert")
async def smoke_alert(req: Request):
    """
    Handles emergency smoke alerts dispatched by the ESP32 sensors.
    Determines severity and generates appropriate text-to-speech warnings.
    """
    body = await req.json()
    adc_val    = body.get("adc_value", 0)
    session_id = body.get("session_id", "")

    current_sess = store.get(session_id)
    username = current_sess.username if current_sess else ""

    # Severe smoke detection generates an evacuation order
    if adc_val >= 2000:
        text = (
            f"Warning{', ' + username if username else ''}! "
            "High smoke concentration detected in the lab. "
            "Please evacuate immediately and open the windows."
        )
    # Lower values denote caution and automated ventilation
    else:
        text = (
            f"Caution{', ' + username if username else ''}. "
            "Mild gas or smoke detected. "
            "Ventilation system has been activated."
        )

    log.warning(f"[SMOKE ALERT] ADC Value = {adc_val} -> Announcement: '{text}'")

    audio_pcm = await asyncio.get_event_loop().run_in_executor(
        None, tts.synthesize, text
    )

    if not audio_pcm:
        return Response(status_code=204)

    # Encrypt the alert audio and return it for playback
    if current_sess:
        encrypted = crypto.encrypt(current_sess.key_bytes, audio_pcm)
        return Response(content=encrypted, media_type="application/octet-stream")
        
    return Response(content=audio_pcm, media_type="application/octet-stream")


@app.get("/api/session/{session_id}/data")
async def session_data(session_id: str):
    """
    Provides full session state to the Web UI, including chat message history, 
    sensor metrics, and active username.
    """
    sess = store.get(session_id)
    if not sess:
        return JSONResponse({"error": "Session not found"}, status_code=404)
        
    return {
        "messages":  sess.messages,
        "sensors":   sess.sensors,
        "has_photo": sess.image_b64 is not None,
        "username":  sess.username,
    }


@app.get("/sessions")
async def list_sessions():
    """
    Provides a high-level summary of all currently active sessions for administration purposes.
    """
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


@app.get("/health")
async def health():
    """
    A simple diagnostics endpoint. Reports active sessions and RAG document count.
    """
    return {
        "status":   "ok",
        "sessions": len(store._sessions),
        "rag_docs": rag._collection.count() if rag._collection else 0,
    }


# Fallback routing to serve the compiled frontend Web UI assets
_WEB_UI_DIR = Path(__file__).parent.parent / "web_ui"

if _WEB_UI_DIR.exists():
    app.mount("/", StaticFiles(directory=str(_WEB_UI_DIR), html=True), name="web_ui")
