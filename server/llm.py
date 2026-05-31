"""
llm.py — Ollama LLM/VLM istemcisi (Faza 2+)
Model: gemma3:4b — Türkçe + vision desteği
Endpoint: http://localhost:11434/api/chat
"""

import logging
import httpx

log = logging.getLogger("llm")

OLLAMA_URL         = "http://localhost:11434/api/chat"
MODEL_NAME         = "gemma3:4b"
TIMEOUT_SEC        = 60.0   # text-only
TIMEOUT_VISION_SEC = 120.0  # with image

SYSTEM_PROMPT = (
    "You are SmartLab Assistant, an AI assistant for an engineering lab.\n"
    "Rules:\n"
    "- Always respond in English\n"
    "- Keep answers to 2-3 sentences, be concise\n"
    "- State safety warnings first if relevant\n"
    "- Use precise technical terminology\n"
    "- If an image is provided: FIRST describe what you see in the image, "
    "then answer the question based on what is visible. "
    "Do NOT ignore the image. The image is the primary context.\n"
    "- Use [Lab Manual Context] only when it directly relates to what is shown "
    "in the image or asked in the question. Ignore it if unrelated.\n"
)


async def generate(
    transcript: str,
    username: str,
    image_b64: str | None = None,
    rag_context: str | None = None,
) -> str:
    """
    Ollama ile Türkçe yanıt üretir.

    image_b64   : JPEG/PNG base64 string — vision modeli için
    rag_context : ChromaDB'den gelen PDF paragrafları
    Fallback    : Ollama erişilemezse kural tabanlı yanıt döner.
    """
    user_parts = [f"User ({username}): {transcript}"]

    # RAG context: görüntü varsa gönderme — LLM'in görüntüyü önceliklendirmesi için
    if rag_context and not image_b64:
        user_parts.append(f"\n[Lab Manual Context]:\n{rag_context}")

    msg: dict = {"role": "user", "content": "\n".join(user_parts)}
    if image_b64:
        msg["images"] = [image_b64]

    payload = {
        "model":   MODEL_NAME,
        "stream":  False,
        "options": {"temperature": 0.7, "num_predict": 60},
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            msg,
        ],
    }

    timeout = TIMEOUT_VISION_SEC if image_b64 else TIMEOUT_SEC
    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            resp = await client.post(OLLAMA_URL, json=payload)
            resp.raise_for_status()
            answer = resp.json()["message"]["content"].strip()
            log.info(f"LLM: '{answer[:80]}{'...' if len(answer) > 80 else ''}'")
            return answer

    except httpx.ConnectError:
        log.warning("Ollama bağlanamadı — fallback devrede")
        return _fallback(transcript, username)
    except httpx.HTTPStatusError as e:
        log.error(f"Ollama HTTP {e.response.status_code}: {e.response.text[:200]}")
        return _fallback(transcript, username)
    except Exception as e:
        log.error(f"LLM beklenmeyen hata: {e}", exc_info=True)
        return _fallback(transcript, username)


def _fallback(transcript: str, username: str) -> str:
    greet = f"Hello {username}! " if username and username not in ("Misafir", "Guest") else ""
    if not transcript.strip():
        return greet + "I didn't catch that. Please try again."
    low = transcript.lower()
    if any(w in low for w in ["hello", "hi", "hey"]):
        return greet + "Hello! How can I help you with the lab equipment?"
    if any(w in low for w in ["test", "check", "working"]):
        return greet + "System is up. Start Ollama to enable AI responses."
    return (
        greet
        + "Ollama is not running. Run 'ollama serve' and 'ollama pull gemma3:4b'."
    )
