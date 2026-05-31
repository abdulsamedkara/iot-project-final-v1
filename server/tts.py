"""
tts.py — Piper TTS (Python API, in-memory model)

subprocess yerine doğrudan PiperVoice API kullanır.
Model ilk çağrıda yüklenir, sonraki çağrılarda RAM'den çalışır.
subprocess overhead yok → ~5-10x daha hızlı.
"""

import io
import wave
import logging

log = logging.getLogger("tts")

_HERE = __import__("os").path.dirname(__import__("os").path.abspath(__file__))
TTS_MODEL_PATH  = _HERE + "/tts_models/en_US-lessac-medium.onnx"
TTS_SAMPLE_RATE = 22050

_voice = None


def _get_voice():
    global _voice
    if _voice is None:
        from piper.voice import PiperVoice
        log.info(f"Piper modeli yükleniyor: {TTS_MODEL_PATH}")
        _voice = PiperVoice.load(TTS_MODEL_PATH)
        log.info("Piper hazır (in-memory).")
    return _voice


def synthesize(text: str) -> bytes:
    """
    Metni PCM'ye çevirir (22050Hz, 16-bit, mono).
    Model RAM'de tutulur — ilk çağrı yavaş, sonrakiler hızlı.
    """
    if not text.strip():
        return b""

    try:
        voice = _get_voice()

        if hasattr(voice, "synthesize_wav"):
            # Piper 1.x: synthesize_wav → WAV bytes
            wav_bytes = voice.synthesize_wav(text)
            if isinstance(wav_bytes, (bytes, bytearray)) and len(wav_bytes) > 44:
                pcm = bytes(wav_bytes[44:])  # WAV header 44 byte
            else:
                # BytesIO veya başka tip
                raw = getattr(wav_bytes, "getvalue", lambda: wav_bytes)()
                pcm = bytes(raw[44:])
        elif hasattr(voice, "synthesize_stream_raw"):
            chunks = [c for c in voice.synthesize_stream_raw(text)]
            pcm = b"".join(chunks)
        else:
            # synthesize(text, wav_file) → wav dosyasına yazar
            buf = io.BytesIO()
            with wave.open(buf, "wb") as wf:
                voice.synthesize(text, wf)
            pcm = buf.getvalue()[44:]

        log.info(f"TTS: {len(text)} kar → {len(pcm)} byte PCM")
        return pcm

    except Exception as e:
        log.error(f"TTS hatası: {e}", exc_info=True)
        return _subprocess_fallback(text)


def _subprocess_fallback(text: str) -> bytes:
    """Python API başarısız olursa eski subprocess yöntemine düş."""
    import subprocess, os
    piper_bin = os.path.join(_HERE, "piper.exe")
    try:
        r = subprocess.run(
            [piper_bin, "--model", TTS_MODEL_PATH, "--output_raw"],
            input=text.encode("utf-8"),
            capture_output=True, timeout=30, cwd=_HERE,
        )
        if r.returncode == 0:
            log.info(f"TTS fallback: {len(r.stdout)} byte")
            return r.stdout
    except Exception as e2:
        log.error(f"TTS subprocess fallback hatası: {e2}")
    return b""


def pcm_to_wav(pcm: bytes, sample_rate: int = TTS_SAMPLE_RATE) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)
    return buf.getvalue()
