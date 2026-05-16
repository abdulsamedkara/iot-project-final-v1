"""
stt.py — Faster-Whisper STT modülü
Ham PCM (16kHz, 16-bit, mono) alır, metne çevirir.
Model ilk çağrıda yüklenir (lazy loading).
"""

import io
import struct
import wave
import logging

log = logging.getLogger("stt")

_model = None
_MODEL_NAME = "base"   # "tiny" daha hızlı, "small" daha doğru
_DEVICE      = "cuda"  # GPU yoksa "cpu" yap
_COMPUTE_TYPE = "float16"  # GPU: float16 | CPU: int8


def _get_model():
    global _model
    if _model is None:
        from faster_whisper import WhisperModel
        log.info(f"Whisper '{_MODEL_NAME}' modeli yükleniyor ({_DEVICE})...")
        _model = WhisperModel(_MODEL_NAME, device=_DEVICE, compute_type=_COMPUTE_TYPE)
        log.info("Whisper hazır.")
    return _model


def pcm_to_wav(pcm_bytes: bytes, sample_rate: int = 16000) -> bytes:
    """Ham PCM → WAV başlığı ekler (Whisper WAV okur)."""
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)    # 16-bit
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_bytes)
    return buf.getvalue()


def transcribe(pcm_bytes: bytes, sample_rate: int = 16000,
               language: str = "en") -> str:
    """
    PCM'yi metne çevirir.
    language="tr" → Türkçe zorla. None → dil tespiti.
    """
    model = _get_model()
    wav_bytes = pcm_to_wav(pcm_bytes, sample_rate)
    audio_file = io.BytesIO(wav_bytes)

    segments, info = model.transcribe(
        audio_file,
        language=language,
        beam_size=5,
        vad_filter=True,          # Sessiz bölümleri filtrele
        vad_parameters={"min_silence_duration_ms": 500},
    )

    text = " ".join(seg.text.strip() for seg in segments)
    log.info(f"STT: '{text}' (dil: {info.language}, olasılık: {info.language_probability:.2f})")
    return text.strip()
