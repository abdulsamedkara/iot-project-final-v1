"""
stt.py

Handles Speech-to-Text (STT) transcription using the Faster-Whisper library.
This module takes raw PCM audio data (16kHz, 16-bit, mono) and converts it to text.
The speech recognition model is loaded lazily on the first function call to save resources.
"""

import io
import struct
import wave
import logging

log = logging.getLogger("stt")

# Store the loaded model instance globally
_model = None

# Configure the Whisper model size. 'tiny' is fastest, 'base' or 'small' are more accurate.
_MODEL_NAME = "tiny"   

# Determine if a GPU with CUDA is available, otherwise fall back to CPU processing
try:
    import torch
    _DEVICE = "cuda" if torch.cuda.is_available() else "cpu"
except ImportError:
    _DEVICE = "cpu"

# Optimize compute type based on the available hardware
_COMPUTE_TYPE = "float16" if _DEVICE == "cuda" else "int8"


def _get_model():
    """
    Lazily loads and returns the Whisper model.
    The model is initialized only when this function is first called.
    """
    global _model
    if _model is None:
        from faster_whisper import WhisperModel
        log.info(f"Loading Whisper '{_MODEL_NAME}' model on device '{_DEVICE}'...")
        _model = WhisperModel(_MODEL_NAME, device=_DEVICE, compute_type=_COMPUTE_TYPE)
        log.info("Whisper model is ready.")
    return _model


def pcm_to_wav(pcm_bytes: bytes, sample_rate: int = 16000) -> bytes:
    """
    Converts raw PCM audio bytes into a WAV formatted byte string.
    This is necessary because the Whisper model expects audio data with a standard WAV header.
    
    Arguments:
    - pcm_bytes: Raw audio data.
    - sample_rate: The sample rate of the audio data (default is 16kHz).
    """
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)      # Mono audio
        wf.setsampwidth(2)      # 16-bit audio depth
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_bytes)
    return buf.getvalue()


def transcribe(pcm_bytes: bytes, sample_rate: int = 16000, language: str = "en") -> str:
    """
    Transcribes the given raw PCM audio data into text.
    
    Arguments:
    - pcm_bytes: The raw audio data.
    - sample_rate: The sample rate of the audio (default is 16kHz).
    - language: The expected language code (e.g., 'en' for English, 'tr' for Turkish).
                If set to None, the model will attempt to auto-detect the language.
                
    Returns:
    - The transcribed text as a string.
    """
    model = _get_model()
    
    # Convert the raw PCM data to WAV format in memory
    wav_bytes = pcm_to_wav(pcm_bytes, sample_rate)
    audio_file = io.BytesIO(wav_bytes)

    # Run the transcription process using the loaded model
    segments, info = model.transcribe(
        audio_file,
        language=language,
        beam_size=1,
        vad_filter=True,          # Enable Voice Activity Detection to filter out silent parts
        vad_parameters={"min_silence_duration_ms": 500},
    )

    # Concatenate all transcribed segments into a single string
    text = " ".join(seg.text.strip() for seg in segments)
    
    log.info(f"STT Result: '{text}' (Detected language: {info.language}, Probability: {info.language_probability:.2f})")
    
    return text.strip()
