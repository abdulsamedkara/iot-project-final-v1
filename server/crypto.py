"""
crypto.py

Handles server-side AES-256-CBC encryption and decryption.
This module ensures secure communication with the ESP32 microcontroller
by using the identical protocol: an Initialization Vector (IV) of 16 bytes 
followed by the AES-CBC encrypted data.
"""

import os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

# AES configuration constants
AES_KEY_LEN = 32   # AES-256 requires a 256-bit (32 bytes) key
AES_IV_LEN  = 16   # The Initialization Vector is a 128-bit (16 bytes) block
AES_BLOCK   = 16   # AES block size is 16 bytes

# Use the default cryptographic backend for operations
_BACKEND = default_backend()


def pad_pkcs7(data: bytes) -> bytes:
    """
    Applies PKCS#7 padding to the input data.
    This ensures the data length becomes a multiple of the AES block size (16 bytes),
    which is a requirement for the CBC encryption mode.
    """
    pad_len = AES_BLOCK - (len(data) % AES_BLOCK)
    return data + bytes([pad_len] * pad_len)


def unpad_pkcs7(data: bytes) -> bytes:
    """
    Removes PKCS#7 padding from the decrypted data.
    It reads the value of the last byte to determine the padding length,
    then strips that many bytes from the end of the data.
    """
    if not data:
        return data
    pad_len = data[-1]
    
    # If the padding length is invalid, return the raw data without unpadding
    if pad_len < 1 or pad_len > AES_BLOCK:
        return data  
        
    return data[:-pad_len]


def encrypt(key: bytes, plaintext: bytes) -> bytes:
    """
    Encrypts the provided plaintext using AES-256-CBC.
    
    The function performs the following steps:
    1. Generates a random 16-byte IV for secure encryption.
    2. Applies PKCS#7 padding to the plaintext.
    3. Encrypts the padded data using the provided 32-byte key and the generated IV.
    4. Prepends the IV to the resulting ciphertext.
    
    Returns:
        The 16-byte IV concatenated with the encrypted data.
    """
    assert len(key) == AES_KEY_LEN, f"Key must be 32 bytes, got {len(key)}"
    
    # Generate a cryptographically secure random IV
    iv = os.urandom(AES_IV_LEN)
    padded = pad_pkcs7(plaintext)
    
    # Initialize the cipher context and encrypt
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=_BACKEND)
    enc = cipher.encryptor()
    ciphertext = enc.update(padded) + enc.finalize()
    
    return iv + ciphertext


def decrypt(key: bytes, data: bytes) -> bytes:
    """
    Decrypts the provided data using AES-256-CBC.
    
    The function expects the input to contain the 16-byte IV followed by the 
    encrypted ciphertext. It extracts the IV, decrypts the ciphertext, and 
    removes the PKCS#7 padding to recover the original plaintext.
    
    Returns:
        The decrypted, unpadded plaintext bytes.
    """
    assert len(key) == AES_KEY_LEN, f"Key must be 32 bytes, got {len(key)}"
    
    if len(data) < AES_IV_LEN + AES_BLOCK:
        raise ValueError(f"Data too short: {len(data)} bytes")
        
    # Extract the IV and the actual ciphertext from the input data
    iv = data[:AES_IV_LEN]
    ciphertext = data[AES_IV_LEN:]
    
    # Initialize the cipher context and decrypt
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv), backend=_BACKEND)
    dec = cipher.decryptor()
    padded = dec.update(ciphertext) + dec.finalize()
    
    # Remove padding to retrieve the original plaintext
    return unpad_pkcs7(padded)
