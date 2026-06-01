"""
llm.py

Acts as the client for the Ollama Large Language Model (LLM) and Vision-Language Model (VLM).
This module connects to a local Ollama instance running the 'gemma3:4b' model to handle
both text-based and vision-based generation requests.
"""

import logging
import httpx

# Configure logging for the LLM module
log = logging.getLogger("llm")

# Ollama API configuration
OLLAMA_URL         = "http://localhost:11434/api/chat"
MODEL_NAME         = "gemma3:4b"

# Define timeouts. Vision requests require more time to process images.
TIMEOUT_SEC        = 60.0   # Timeout for text-only requests
TIMEOUT_VISION_SEC = 120.0  # Timeout for vision-based requests

# System prompt defining the AI's persona, rules, and behavior
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
    Generates an AI response using the Ollama API based on user input, context, and optional images.
    
    Arguments:
    - transcript: The transcribed text of what the user said.
    - username: The name of the user making the request.
    - image_b64: A base64-encoded string of an image, if provided.
    - rag_context: Retrieved document paragraphs from the knowledge base.
    
    Returns:
    - The generated response text. If Ollama is unreachable, returns a fallback response.
    """
    # Build the user's message incorporating their name and spoken transcript
    user_parts = [f"User ({username}): {transcript}"]

    # Append RAG context only if there is no image. This ensures the LLM
    # prioritizes analyzing the image when one is provided.
    if rag_context and not image_b64:
        user_parts.append(f"\n[Lab Manual Context]:\n{rag_context}")

    # Construct the message dictionary for the Ollama API
    msg: dict = {"role": "user", "content": "\n".join(user_parts)}
    
    # Attach the image if one is provided
    if image_b64:
        msg["images"] = [image_b64]

    # Prepare the full payload for the API request
    payload = {
        "model":   MODEL_NAME,
        "stream":  False,
        "options": {
            "temperature": 0.7, 
            "num_predict": 60
        },
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            msg,
        ],
    }

    # Select the appropriate timeout based on whether an image is included
    timeout = TIMEOUT_VISION_SEC if image_b64 else TIMEOUT_SEC
    
    try:
        # Make the asynchronous HTTP POST request to Ollama
        async with httpx.AsyncClient(timeout=timeout) as client:
            resp = await client.post(OLLAMA_URL, json=payload)
            resp.raise_for_status()
            
            # Extract and log the generated answer
            answer = resp.json()["message"]["content"].strip()
            log.info(f"LLM: '{answer[:80]}{'...' if len(answer) > 80 else ''}'")
            return answer

    except httpx.ConnectError:
        log.warning("Could not connect to Ollama. Fallback activated.")
        return _fallback(transcript, username)
    except httpx.HTTPStatusError as e:
        log.error(f"Ollama HTTP error {e.response.status_code}: {e.response.text[:200]}")
        return _fallback(transcript, username)
    except Exception as e:
        log.error(f"Unexpected LLM error: {e}", exc_info=True)
        return _fallback(transcript, username)


def _fallback(transcript: str, username: str) -> str:
    """
    Provides a simple rule-based fallback response if the Ollama service is down or unreachable.
    It checks for basic keywords in the user's transcript to provide helpful feedback.
    """
    # Create a greeting if the user is known and not a generic guest
    greet = f"Hello {username}! " if username and username not in ("Misafir", "Guest") else ""
    
    if not transcript.strip():
        return greet + "I didn't catch that. Please try again."
        
    low = transcript.lower()
    
    # Respond to basic greetings
    if any(w in low for w in ["hello", "hi", "hey"]):
        return greet + "Hello! How can I help you with the lab equipment?"
        
    # Respond to system status checks
    if any(w in low for w in ["test", "check", "working"]):
        return greet + "System is up. Start Ollama to enable AI responses."
        
    # Default fallback message explaining that the AI backend is offline
    return (
        greet
        + "Ollama is not running. Please run 'ollama serve' and 'ollama pull gemma3:4b' on the server."
    )
