"""
tts.py

Handles Text-to-Speech (TTS) synthesis using Piper TTS via its Python API.
The speech model is loaded into memory on the first call, eliminating subprocess
overhead and making audio generation approximately 5-10 times faster.
"""

import io
import wave
import logging

log = logging.getLogger("tts")

# Resolve the current directory path dynamically
_HERE = __import__("os").path.dirname(__import__("os").path.abspath(__file__))

# Path to the Piper TTS model file
TTS_MODEL_PATH  = _HERE + "/tts_models/en_US-lessac-medium.onnx"

# The target sample rate for the output audio
TTS_SAMPLE_RATE = 22050

# Store the loaded voice instance globally
_voice = None


def _get_voice():
    """
    Lazily loads and returns the PiperVoice model.
    The model is kept in memory to speed up subsequent synthesis requests.
    """
    global _voice
    if _voice is None:
        from piper.voice import PiperVoice
        log.info(f"Loading Piper TTS model from: {TTS_MODEL_PATH}")
        _voice = PiperVoice.load(TTS_MODEL_PATH)
        log.info("Piper TTS model is ready in memory.")
    return _voice


def synthesize(text: str) -> bytes:
    """
    Converts the provided text into raw PCM audio data (22050Hz, 16-bit, mono).
    
    Because the model is loaded into RAM, the first invocation might take slightly longer,
    but all subsequent calls will execute rapidly.
    
    Arguments:
    - text: The text to be spoken.
    
    Returns:
    - Raw PCM audio bytes.
    """
    if not text.strip():
        return b""

    try:
        voice = _get_voice()

        # Check for the available synthesis method on the voice object
        # It handles different PiperVoice API versions
        if hasattr(voice, "synthesize_wav"):
            buf = io.BytesIO()
            with wave.open(buf, "wb") as wf:
                voice.synthesize_wav(text, wf)
            # Skip the 44-byte WAV header to return raw PCM
            pcm = buf.getvalue()[44:]
            
        elif hasattr(voice, "synthesize_stream_raw"):
            # Generates a raw stream of PCM chunks
            chunks = [c for c in voice.synthesize_stream_raw(text)]
            pcm = b"".join(chunks)
            
        else:
            # Fallback for other API versions that write directly to a WAV file stream
            buf = io.BytesIO()
            with wave.open(buf, "wb") as wf:
                voice.synthesize(text, wf)
            # Skip the 44-byte WAV header
            pcm = buf.getvalue()[44:]

        log.info(f"TTS synthesis complete: {len(text)} characters converted to {len(pcm)} bytes of PCM audio.")
        return pcm

    except Exception as e:
        log.error(f"Error during TTS synthesis: {e}", exc_info=True)
        # If the direct API approach fails, use the CLI subprocess fallback
        return _subprocess_fallback(text)


def _subprocess_fallback(text: str) -> bytes:
    """
    A fallback method that uses the Piper executable in a subprocess.
    This is triggered only if the in-memory Python API synthesis encounters an error.
    """
    import subprocess, os
    piper_bin = os.path.join(_HERE, "piper.exe")
    
    try:
        # Run the piper binary and capture its standard output
        r = subprocess.run(
            [piper_bin, "--model", TTS_MODEL_PATH, "--output_raw"],
            input=text.encode("utf-8"),
            capture_output=True, 
            timeout=30, 
            cwd=_HERE,
        )
        if r.returncode == 0:
            log.info(f"TTS fallback generated {len(r.stdout)} bytes of audio data.")
            return r.stdout
    except Exception as e2:
        log.error(f"TTS subprocess fallback also failed: {e2}")
        
    return b""


def pcm_to_wav(pcm: bytes, sample_rate: int = TTS_SAMPLE_RATE) -> bytes:
    """
    Utility function to convert raw PCM data back into a valid WAV format by adding a header.
    """
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)           # Mono audio
        wf.setsampwidth(2)           # 16-bit audio
        wf.setframerate(sample_rate) # Target sample rate
        wf.writeframes(pcm)
    return buf.getvalue()
