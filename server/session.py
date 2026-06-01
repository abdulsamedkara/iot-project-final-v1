"""
session.py

Manages WebSocket sessions for the SmartLab application.
This module handles tracking active connections, associating them with user data,
and securely managing AES-256 session keys.

Each WebSocket connection contains:
- A unique session identifier (UUID)
- An AES-256 session key (32 random bytes for encryption)
- The user's name (mapped via RFID UID)
- Connection timestamp
- Chat history and current sensor states
"""

import secrets
import base64
import time
import threading
from dataclasses import dataclass, field
from typing import Optional

# A simple user database mapping RFID hex codes to user names.
# In a full-scale application, this should be replaced with a real database.
RFID_USERS: dict[str, str] = {
    "A1B2C3D4": "Abdulsamet Kara",
    "F1B00C07": "Abdul Samed Kara",
    "11223344": "Test User",
    "635113FD": "Mert Abdullahoglu",
    # Additional cards can be registered here using the format "HEXUID": "Full Name"
}


@dataclass
class Session:
    """
    Represents an active connection session containing the user's state,
    cryptographic keys, and real-time interaction data.
    """
    session_id: str
    key_bytes: bytes                               # 32-byte AES-256 encryption key
    created_at: float = field(default_factory=time.time)
    username: str = "Guest"
    rfid_uid: Optional[str] = None
    image_b64: Optional[str] = None
    messages: list = field(default_factory=list)   # Stores chat history as a list of dicts: [{role, text, ts}]
    sensors: dict = field(default_factory=dict)    # Stores the latest sensor readings from the hardware

    @property
    def key_b64(self) -> str:
        """
        Returns the session key encoded in Base64 format.
        This string is sent to the ESP32 hardware to establish the encryption protocol.
        """
        return base64.b64encode(self.key_bytes).decode()


class SessionStore:
    """
    Thread-safe storage for managing all active application sessions.
    """
    def __init__(self):
        self._sessions: dict[str, Session] = {}
        self._lock = threading.Lock()

    def create(self) -> Session:
        """
        Creates a new session with cryptographically secure random identifiers and keys,
        then stores it in the active session dictionary.
        """
        sess = Session(
            session_id=secrets.token_hex(16),
            key_bytes=secrets.token_bytes(32),     # Generate a high-quality cryptographic key
        )
        with self._lock:
            self._sessions[sess.session_id] = sess
        return sess

    def get(self, session_id: str) -> Optional[Session]:
        """
        Retrieves a session by its unique identifier.
        """
        with self._lock:
            return self._sessions.get(session_id)

    def set_user(self, session_id: str, rfid_uid: str) -> str:
        """
        Links an RFID tag UID to a session and sets the corresponding username
        from the simulated user database.
        """
        # Lookup the name based on the scanned RFID, defaulting to Unknown if not found
        username = RFID_USERS.get(rfid_uid.upper(), f"Unknown ({rfid_uid})")
        with self._lock:
            sess = self._sessions.get(session_id)
            if sess:
                sess.rfid_uid = rfid_uid
                sess.username = username
        return username

    def set_image(self, session_id: str, image_b64: str) -> bool:
        """
        Attaches a base64-encoded image to the session for vision model processing.
        Returns True if the session was found and updated successfully.
        """
        with self._lock:
            sess = self._sessions.get(session_id)
            if sess:
                sess.image_b64 = image_b64
                return True
            return False

    def clear_user(self, session_id: str):
        """
        Resets the user's specific data (RFID, photo, and chat messages) effectively logging them out,
        while keeping the underlying WebSocket connection and encryption session open.
        """
        with self._lock:
            sess = self._sessions.get(session_id)
            if sess:
                sess.rfid_uid = None
                sess.username = "Guest"
                sess.image_b64 = None
                sess.messages = []

    def remove(self, session_id: str):
        """
        Completely removes a session from the store when a connection is closed.
        """
        with self._lock:
            self._sessions.pop(session_id, None)

    def cleanup_old(self, max_age_sec: int = 3600):
        """
        Iterates through all active sessions and removes those that are older than
        the specified maximum age (default is 1 hour).
        """
        now = time.time()
        with self._lock:
            # Identify sessions that have exceeded the maximum allowed age
            stale = [sid for sid, s in self._sessions.items()
                     if now - s.created_at > max_age_sec]
            
            # Delete stale sessions from tracking
            for sid in stale:
                del self._sessions[sid]


# Create a global instance of the SessionStore to be imported and used across the application (e.g., in main.py)
store = SessionStore()
