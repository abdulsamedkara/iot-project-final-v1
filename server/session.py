"""
session.py — SmartLab WebSocket oturum yönetimi

Her WebSocket bağlantısı için:
  - session_id (UUID)
  - AES-256 oturum anahtarı (32 rastgele byte)
  - kullanıcı adı (RFID UID'ye göre)
  - bağlantı zamanı
"""

import secrets
import base64
import time
import threading
from dataclasses import dataclass, field
from typing import Optional

# ─── Basit kullanıcı veritabanı (gerçek projede DB'ye taşı) ──────────────────
RFID_USERS: dict[str, str] = {
    "A1B2C3D4": "Abdulsamet Kara",
    "11223344": "Test Kullanicisi",
    "635113FD": "Mert Abdullahoğlu",
    # Yeni kartlar eklemek için: "HEXUID": "Ad Soyad"
}


@dataclass
class Session:
    session_id: str
    key_bytes: bytes          # 32 byte AES-256 anahtarı
    created_at: float = field(default_factory=time.time)
    username: str = "Misafir"
    rfid_uid: Optional[str] = None
    image_b64: Optional[str] = None   # Faza 4: mobil web UI'dan yüklenen fotoğraf

    @property
    def key_b64(self) -> str:
        """Base64 kodlu anahtar — ESP32'ye gönderilir."""
        return base64.b64encode(self.key_bytes).decode()


class SessionStore:
    def __init__(self):
        self._sessions: dict[str, Session] = {}
        self._lock = threading.Lock()

    def create(self) -> Session:
        """Yeni session oluşturur ve depolar."""
        sess = Session(
            session_id=secrets.token_hex(16),
            key_bytes=secrets.token_bytes(32),   # kriptografik kalite
        )
        with self._lock:
            self._sessions[sess.session_id] = sess
        return sess

    def get(self, session_id: str) -> Optional[Session]:
        with self._lock:
            return self._sessions.get(session_id)

    def set_user(self, session_id: str, rfid_uid: str) -> str:
        """RFID UID'ye göre kullanıcı adını ayarlar."""
        username = RFID_USERS.get(rfid_uid.upper(), f"Bilinmeyen ({rfid_uid})")
        with self._lock:
            sess = self._sessions.get(session_id)
            if sess:
                sess.rfid_uid = rfid_uid
                sess.username = username
        return username

    def set_image(self, session_id: str, image_b64: str) -> bool:
        """Fotoğrafı session'a ekler. Başarılıysa True döner."""
        with self._lock:
            sess = self._sessions.get(session_id)
            if sess:
                sess.image_b64 = image_b64
                return True
            return False

    def remove(self, session_id: str):
        with self._lock:
            self._sessions.pop(session_id, None)

    def cleanup_old(self, max_age_sec: int = 3600):
        """1 saatten eski session'ları temizler."""
        now = time.time()
        with self._lock:
            stale = [sid for sid, s in self._sessions.items()
                     if now - s.created_at > max_age_sec]
            for sid in stale:
                del self._sessions[sid]


# Global store — main.py'de import edilir
store = SessionStore()
