"""
crypto.py — Sunucu taraflı AES-256-CBC şifreleme/deşifre
ESP32 ile aynı protokol: IV(16) | AES-CBC(veri)
"""

import os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

AES_KEY_LEN = 32   # 256 bit
AES_IV_LEN  = 16   # 128 bit blok
AES_BLOCK   = 16

_BACKEND = default_backend()


def pad_pkcs7(data: bytes) -> bytes:
    """PKCS#7 padding ekler."""
    pad_len = AES_BLOCK - (len(data) % AES_BLOCK)
    return data + bytes([pad_len] * pad_len)


def unpad_pkcs7(data: bytes) -> bytes:
    """PKCS#7 padding çıkarır."""
    if not data:
        return data
    pad_len = data[-1]
    if pad_len < 1 or pad_len > AES_BLOCK:
        return data  # Geçersiz pad → ham veri döndür
    return data[:-pad_len]


def encrypt(key: bytes, plaintext: bytes) -> bytes:
    """
    AES-256-CBC ile şifreler.
    Çıktı: IV(16 byte) | şifreli veri
    """
    assert len(key) == AES_KEY_LEN, f"Anahtar 32 byte olmalı, {len(key)} geldi"
    iv = os.urandom(AES_IV_LEN)
    padded = pad_pkcs7(plaintext)
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=_BACKEND)
    enc = cipher.encryptor()
    ciphertext = enc.update(padded) + enc.finalize()
    return iv + ciphertext


def decrypt(key: bytes, data: bytes) -> bytes:
    """
    AES-256-CBC ile deşifre eder.
    Girdi: IV(16 byte) | şifreli veri
    """
    assert len(key) == AES_KEY_LEN
    if len(data) < AES_IV_LEN + AES_BLOCK:
        raise ValueError(f"Çok kısa veri: {len(data)} byte")
    iv         = data[:AES_IV_LEN]
    ciphertext = data[AES_IV_LEN:]
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=_BACKEND)
    dec = cipher.decryptor()
    padded = dec.update(ciphertext) + dec.finalize()
    return unpad_pkcs7(padded)
