"""
tts.py — Piper TTS modülü
Metin alır, 22050Hz 16-bit mono PCM döndürür.
Vize projesindeki voice_server.py'den uyarlandı.

Piper kurulum:
  pip install piper-tts
  piper --download-dir ./tts_models --model tr_TR-dfki-medium

Kullanım:
  audio_bytes = synthesize("Merhaba, nasılsınız?")
"""

import io
import subprocess
import logging
import struct
import wave

log = logging.getLogger("tts")

# Piper model yolu — kendi modelini buraya yaz
TTS_MODEL_PATH = "./tts_models/en_US-lessac-medium.onnx"
TTS_SAMPLE_RATE = 22050

# Piper binary yolu (pip ile kurulunca PATH'te olur)
PIPER_BIN = "piper"


def synthesize(text: str) -> bytes:
    """
    Metni sese çevirir.
    Döndürülen bytes: ham PCM (22050Hz, 16-bit, mono)
    """
    if not text.strip():
        return b""

    try:
        result = subprocess.run(
            [
                PIPER_BIN,
                "--model",  TTS_MODEL_PATH,
                "--output_raw",          # Ham PCM çıktısı
            ],
            input=text.encode("utf-8"),
            capture_output=True,
            timeout=30,
        )

        if result.returncode != 0:
            log.error(f"Piper hatası: {result.stderr.decode()}")
            return b""

        pcm = result.stdout
        log.info(f"TTS: {len(text)} karakter → {len(pcm)} byte PCM")
        return pcm

    except subprocess.TimeoutExpired:
        log.error("Piper zaman aşımı")
        return b""
    except FileNotFoundError:
        log.error(f"Piper bulunamadı: '{PIPER_BIN}'. "
                  "Kurulum: pip install piper-tts")
        return b""


def pcm_to_wav(pcm: bytes, sample_rate: int = TTS_SAMPLE_RATE) -> bytes:
    """Ham PCM'yi WAV formatına sarar (hata ayıklama için)."""
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)
    return buf.getvalue()
