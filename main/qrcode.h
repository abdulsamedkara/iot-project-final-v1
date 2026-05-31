#pragma once
#include <stdint.h>
#include <stdbool.h>

// Minimum QR kütüphanesi — byte mode, versiyon 1-4, ECC_LOW
// ricmoo/QRCode (MIT) tabanlı

#define QR_ECC_LOW      0
#define QR_ECC_MEDIUM   1
#define QR_ECC_QUARTILE 2
#define QR_ECC_HIGH     3

typedef struct {
    uint8_t  version;
    uint8_t  size;      // modules: version*4+17
    uint8_t  ecc;
    uint8_t  mask;
    uint8_t *data;      // bit-packed module matrix
} QRCode;

// Buffer boyutu hesapla (versiyon için)
uint16_t qrcode_getBufferSize(uint8_t version);

// Metin → QR matris (başarıysa 0 döner)
int8_t qrcode_initText(QRCode *qrcode, uint8_t *dataBuffer,
                        uint8_t version, uint8_t ecc, const char *text);

// (x,y) modülünün siyah olup olmadığını döndürür
bool qrcode_getModule(QRCode *qrcode, uint8_t x, uint8_t y);
