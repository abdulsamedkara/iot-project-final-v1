// qrcode.c — Minimal QR Code generator
// Byte mode, Version 1-4, tüm ECC seviyeleri
// ricmoo/QRCode (MIT License) tabanlı uyarlama

#include "qrcode.h"
#include <string.h>
#include <stdlib.h>

// ─── GF(256) tabloları — primitif polinom 0x11D ───────────────────────────────
static const uint8_t EXP[256] = {
    1,2,4,8,16,32,64,128,29,58,116,232,205,135,19,38,76,152,45,90,180,117,234,
    201,143,3,6,12,24,48,96,192,157,39,78,156,37,74,148,53,106,212,181,119,238,
    193,159,35,70,140,5,10,20,40,80,160,93,186,105,210,185,111,222,161,95,190,
    97,194,153,47,94,188,101,202,137,15,30,60,120,240,253,231,211,187,107,214,
    177,127,254,225,223,163,91,182,113,226,217,175,67,134,17,34,68,136,13,26,
    52,104,208,189,103,206,129,31,62,124,248,237,199,147,59,118,236,197,151,51,
    102,204,133,23,46,92,184,109,218,169,79,158,33,66,132,21,42,84,168,77,154,
    41,82,164,85,170,73,146,57,114,228,213,183,115,230,209,191,99,198,145,63,
    126,252,229,215,179,123,246,241,255,227,219,171,75,150,49,98,196,149,55,110,
    220,165,87,174,65,130,25,50,100,200,141,7,14,28,56,112,224,221,167,83,166,
    81,162,89,178,121,242,249,239,195,155,43,86,172,69,138,9,18,36,72,144,61,
    122,244,245,247,243,251,235,203,139,11,22,44,88,176,125,250,233,207,131,27,
    54,108,216,173,71,142,1
};
static const uint8_t LOG[256] = {
    0,0,1,25,2,50,26,198,3,223,51,238,27,104,199,75,4,100,224,14,52,141,239,129,
    28,193,105,248,200,8,76,113,5,138,101,47,225,36,15,33,53,147,142,218,240,18,
    130,69,29,181,194,125,106,39,249,185,201,154,9,120,77,228,114,166,6,191,139,
    98,102,221,48,253,226,152,37,179,16,145,34,136,54,208,148,206,143,150,219,
    189,241,210,19,92,131,56,70,64,30,66,182,163,195,72,126,110,107,58,40,84,
    250,133,186,61,202,94,155,159,10,21,121,43,78,212,229,172,115,243,167,87,7,
    112,192,247,140,128,99,13,103,74,222,237,49,197,254,24,227,165,153,119,38,
    184,180,124,17,68,146,217,35,32,137,46,55,63,209,91,149,188,207,205,144,135,
    151,178,220,252,190,97,242,86,211,171,20,42,93,158,132,60,57,83,71,109,65,
    162,31,45,67,216,183,123,164,118,196,23,73,236,127,12,111,246,108,161,59,82,
    41,157,85,170,251,96,134,177,187,204,62,90,203,89,95,176,156,169,160,81,11,
    245,22,235,122,117,44,215,79,174,213,233,230,231,173,232,116,214,244,234,168,
    80,88,175
};

static inline uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return EXP[(LOG[a] + LOG[b]) % 255];
}

// ─── Versiyon kapasite tabloları ─────────────────────────────────────────────
// [version-1][ecc] = {data_codewords, ecc_codewords, blocks}
typedef struct { uint8_t data; uint8_t ecc; uint8_t blocks; } CapEntry;
static const CapEntry CAP[4][4] = {
    // V1: 21x21
    {{19,7,1},{16,10,1},{13,13,1},{9,17,1}},
    // V2: 25x25
    {{34,10,1},{28,16,1},{22,22,1},{16,28,1}},
    // V3: 29x29
    {{55,15,1},{44,26,1},{34,18,2},{26,22,2}},
    // V4: 33x33
    {{80,20,2},{64,18,2},{48,26,4},{36,16,4}},
};

// ─── RS generator polynomials (precomputed) ──────────────────────────────────
// Indexed by ecc count (7,10,13,15,16,17,18,20,22,24,26,28)
static const uint8_t GEN_7[]  = {87,229,146,149,238,102,21};
static const uint8_t GEN_10[] = {251,67,46,61,118,70,64,94,32,45};
static const uint8_t GEN_13[] = {74,152,176,100,86,100,106,104,130,218,206,140,78};
static const uint8_t GEN_15[] = {8,183,61,91,202,37,51,58,58,237,140,124,5,99,105};
static const uint8_t GEN_16[] = {120,104,107,109,102,161,76,3,91,191,147,169,182,194,225,120};
static const uint8_t GEN_17[] = {43,139,206,78,43,239,123,206,214,147,24,99,150,39,243,163,136};
static const uint8_t GEN_18[] = {215,234,158,94,184,97,118,170,79,187,152,148,252,179,5,98,96,153};
static const uint8_t GEN_20[] = {17,60,79,50,61,163,26,187,202,180,221,225,83,239,156,164,212,212,188,190};
static const uint8_t GEN_22[] = {210,171,247,242,93,230,14,109,221,53,200,74,8,172,98,80,219,134,160,105,165,231};
static const uint8_t GEN_24[] = {229,121,135,48,211,117,251,126,159,180,169,152,192,226,228,218,111,0,117,232,87,96,227,21};
static const uint8_t GEN_26[] = {173,125,158,2,103,182,118,17,145,201,111,28,165,53,161,21,245,142,13,102,48,227,153,145,218,70};
static const uint8_t GEN_28[] = {168,223,200,104,224,234,108,180,110,190,195,147,205,27,232,201,21,43,245,87,42,195,212,119,242,37,9,123};

static const uint8_t *get_gen(uint8_t n, uint8_t *out_len)
{
    switch (n) {
    case 7:  *out_len=7;  return GEN_7;
    case 10: *out_len=10; return GEN_10;
    case 13: *out_len=13; return GEN_13;
    case 15: *out_len=15; return GEN_15;
    case 16: *out_len=16; return GEN_16;
    case 17: *out_len=17; return GEN_17;
    case 18: *out_len=18; return GEN_18;
    case 20: *out_len=20; return GEN_20;
    case 22: *out_len=22; return GEN_22;
    case 24: *out_len=24; return GEN_24;
    case 26: *out_len=26; return GEN_26;
    case 28: *out_len=28; return GEN_28;
    default: *out_len=0;  return NULL;
    }
}

// ─── Reed-Solomon ECC hesapla ────────────────────────────────────────────────
static void rs_ecc(const uint8_t *data, uint8_t data_len,
                   const uint8_t *gen, uint8_t gen_len,
                   uint8_t *ecc)
{
    memset(ecc, 0, gen_len);
    for (uint8_t i = 0; i < data_len; i++) {
        uint8_t coef = data[i] ^ ecc[0];
        memmove(ecc, ecc + 1, gen_len - 1);
        ecc[gen_len - 1] = 0;
        for (uint8_t j = 0; j < gen_len; j++) {
            ecc[j] ^= gf_mul(gen[j], coef);
        }
    }
}

// ─── Bit stream yazar ─────────────────────────────────────────────────────────
typedef struct { uint8_t *buf; uint16_t bit_pos; } BitStream;

static void bs_write(BitStream *bs, uint32_t val, uint8_t bits)
{
    for (int i = bits - 1; i >= 0; i--) {
        uint16_t idx = bs->bit_pos >> 3;
        uint8_t  bit = (bs->bit_pos) & 7;
        if ((val >> i) & 1) {
            bs->buf[idx] |= (0x80 >> bit);
        } else {
            bs->buf[idx] &= ~(0x80 >> bit);
        }
        bs->bit_pos++;
    }
}

// ─── Matris modül erişim ──────────────────────────────────────────────────────
static void set_module(QRCode *qr, uint8_t x, uint8_t y, bool dark)
{
    uint16_t p = (uint16_t)y * qr->size + x;
    if (dark) qr->data[p >> 3] |=  (0x80 >> (p & 7));
    else      qr->data[p >> 3] &= ~(0x80 >> (p & 7));
}

static bool get_module_raw(QRCode *qr, uint8_t x, uint8_t y)
{
    uint16_t p = (uint16_t)y * qr->size + x;
    return (qr->data[p >> 3] >> (7 - (p & 7))) & 1;
}

// ─── Finder pattern (7×7 kare) ───────────────────────────────────────────────
static void place_finder(QRCode *qr, int16_t ox, int16_t oy)
{
    for (int8_t dy = -1; dy <= 7; dy++) {
        for (int8_t dx = -1; dx <= 7; dx++) {
            int16_t x = ox + dx, y = oy + dy;
            if (x < 0 || x >= qr->size || y < 0 || y >= qr->size) continue;
            bool dark = (dx == -1 || dx == 7 || dy == -1 || dy == 7) ? false :
                        (dx == 0 || dx == 6 || dy == 0 || dy == 6) ? true :
                        (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4);
            set_module(qr, x, y, dark);
        }
    }
}

// ─── Alignment pattern (5×5 kare) — V2+ için ─────────────────────────────────
static void place_alignment(QRCode *qr, uint8_t cx, uint8_t cy)
{
    for (int8_t dy = -2; dy <= 2; dy++) {
        for (int8_t dx = -2; dx <= 2; dx++) {
            bool dark = (dx == -2 || dx == 2 || dy == -2 || dy == 2) ? true :
                        (dx == 0 && dy == 0) ? true : false;
            set_module(qr, cx + dx, cy + dy, dark);
        }
    }
}

// ─── Timing patterns ─────────────────────────────────────────────────────────
static void place_timing(QRCode *qr)
{
    for (uint8_t i = 8; i < qr->size - 8; i++) {
        bool dark = (i & 1) == 0;
        set_module(qr, i, 6, dark);
        set_module(qr, 6, i, dark);
    }
}

// ─── Dark module ─────────────────────────────────────────────────────────────
static void place_dark(QRCode *qr)
{
    set_module(qr, 8, (uint8_t)(4 * qr->version + 9), true);
}

// ─── İşlevsel modülleri işaretle (veri yerleştirme için) ─────────────────────
// Ayrı bir "function" bitmap kullanmak yerine, veri yerleşimi sırasında
// işlevsel bölgeleri atlayacağız.
static bool is_function(QRCode *qr, uint8_t x, uint8_t y)
{
    uint8_t s = qr->size;
    // Finder + separator
    if ((x <= 7 && y <= 7) ||
        (x >= s-8 && y <= 7) ||
        (x <= 7 && y >= s-8)) return true;
    // Timing
    if (x == 6 || y == 6) return true;
    // Dark module
    if (x == 8 && y == (uint8_t)(4*qr->version+9)) return true;
    // Format info areas
    if (y == 8 && x <= 8) return true;
    if (x == 8 && y <= 8) return true;
    if (y == 8 && x >= s-8) return true;
    if (x == 8 && y >= s-7) return true;
    // Alignment (version 2+)
    if (qr->version >= 2) {
        // V2-4: single alignment at (version*4+10, version*4+10)
        uint8_t ap = (uint8_t)(qr->version * 4 + 10);
        if (x >= ap-2 && x <= ap+2 && y >= ap-2 && y <= ap+2) return true;
    }
    return false;
}

// ─── Format bilgisi ──────────────────────────────────────────────────────────
// ECC + mask bitsini QR'a yerleştir
// Format string = 15 bit (5 bit data + 10 bit ECC), mask 101010000010010
static const uint16_t FORMAT_INFO[32] = {
    0x77C4,0x72F3,0x7DAA,0x789D,0x662F,0x6318,0x6C41,0x6976,
    0x5412,0x5125,0x5E7C,0x5B4B,0x45F9,0x40CE,0x4F97,0x4AA0,
    0x355F,0x3068,0x3F31,0x3A06,0x24B4,0x2183,0x2EDA,0x2BED,
    0x1689,0x13BE,0x1CE7,0x19D0,0x0762,0x0255,0x0D0C,0x083B,
};

static void place_format(QRCode *qr, uint8_t mask_pattern)
{
    uint16_t fmt = FORMAT_INFO[qr->ecc * 8 + mask_pattern];
    uint8_t s = qr->size;

    for (uint8_t i = 0; i < 15; i++) {
        bool dark = (fmt >> i) & 1;
        // Top-left horizontal
        uint8_t col = (i < 6) ? i : (i < 8 ? i+1 : s-15+i);
        uint8_t row = 8;
        if (i < 8) set_module(qr, col, 8, dark);
        else       set_module(qr, s-15+i, 8, dark);

        // Top-left vertical + bottom-left/top-right
        if (i < 8) {
            set_module(qr, 8, (i < 6 ? i : i+1), dark);
        } else {
            set_module(qr, 8, s-7+(i-8), dark);
        }
        (void)row; (void)col;
    }
}

// ─── Veri modüllerini yerleştir ───────────────────────────────────────────────
static void place_data(QRCode *qr, const uint8_t *codewords, uint16_t cw_count,
                        uint8_t mask_pattern)
{
    uint16_t bit_idx = 0;
    int16_t  col = (int16_t)qr->size - 1;

    while (col > 0) {
        if (col == 6) col--;  // timing column
        for (uint8_t row_i = 0; row_i < qr->size; row_i++) {
            uint8_t row = (((qr->size - 1 - col) / 2) & 1) ?
                          row_i : (qr->size - 1 - row_i);
            for (int8_t dc = 0; dc <= 1; dc++) {
                uint8_t x = (uint8_t)(col - dc);
                if (is_function(qr, x, row)) continue;

                bool bit = false;
                if (bit_idx < (uint16_t)(cw_count * 8)) {
                    bit = (codewords[bit_idx >> 3] >> (7 - (bit_idx & 7))) & 1;
                    bit_idx++;
                }

                // Apply mask
                bool inv = false;
                switch (mask_pattern) {
                case 0: inv = ((row + x) % 2 == 0); break;
                case 1: inv = (row % 2 == 0); break;
                case 2: inv = (x % 3 == 0); break;
                case 3: inv = ((row + x) % 3 == 0); break;
                case 4: inv = ((row/2 + x/3) % 2 == 0); break;
                case 5: inv = ((row*x)%2 + (row*x)%3 == 0); break;
                case 6: inv = (((row*x)%2 + (row*x)%3) % 2 == 0); break;
                case 7: inv = (((row+x)%2 + (row*x)%3) % 2 == 0); break;
                }
                set_module(qr, x, row, bit ^ inv);
            }
        }
        col -= 2;
    }
}

// ─── Mask puanlama (basitleştirilmiş, sadece kural 1) ─────────────────────────
static int16_t score_mask(QRCode *qr)
{
    int16_t score = 0;
    for (uint8_t y = 0; y < qr->size; y++) {
        uint8_t run = 1;
        bool    last = get_module_raw(qr, 0, y);
        for (uint8_t x = 1; x < qr->size; x++) {
            bool cur = get_module_raw(qr, x, y);
            if (cur == last) { run++; if (run == 5) score += 3; else if (run > 5) score++; }
            else { run = 1; last = cur; }
        }
    }
    return score;
}

// ─── Public API ──────────────────────────────────────────────────────────────
uint16_t qrcode_getBufferSize(uint8_t version)
{
    uint8_t sz = version * 4 + 17;
    return ((uint16_t)sz * sz + 7) / 8;
}

int8_t qrcode_initText(QRCode *qrcode, uint8_t *dataBuffer,
                        uint8_t version, uint8_t ecc, const char *text)
{
    if (version < 1 || version > 4 || ecc > 3) return -1;

    uint8_t text_len = (uint8_t)strlen(text);
    const CapEntry *cap = &CAP[version-1][ecc];
    if (text_len + 3 > cap->data) return -2;  // doesn't fit (rough check)

    qrcode->version = version;
    qrcode->size    = version * 4 + 17;
    qrcode->ecc     = ecc;
    qrcode->data    = dataBuffer;
    uint16_t buf_size = qrcode_getBufferSize(version);
    memset(dataBuffer, 0, buf_size);

    // ── 1. Encode data codewords ─────────────────────────────────────────────
    static uint8_t cw_buf[160];
    memset(cw_buf, 0, sizeof(cw_buf));
    BitStream bs = { .buf = cw_buf, .bit_pos = 0 };

    bs_write(&bs, 0x4, 4);            // byte mode
    bs_write(&bs, text_len, 8);        // char count
    for (uint8_t i = 0; i < text_len; i++) {
        bs_write(&bs, (uint8_t)text[i], 8);
    }
    bs_write(&bs, 0x0, 4);             // terminator

    // Pad to byte boundary
    while (bs.bit_pos % 8) bs_write(&bs, 0, 1);

    // Pad codewords
    static const uint8_t PAD[2] = {0xEC, 0x11};
    uint8_t cw_count = (uint8_t)(bs.bit_pos / 8);
    uint8_t pad_i = 0;
    while (cw_count < cap->data) {
        bs_write(&bs, PAD[pad_i & 1], 8);
        cw_count++;
        pad_i++;
    }

    // ── 2. ECC codewords ─────────────────────────────────────────────────────
    uint8_t gen_len;
    const uint8_t *gen = get_gen(cap->ecc, &gen_len);
    if (!gen) return -3;

    static uint8_t ecc_buf[30];
    memset(ecc_buf, 0, sizeof(ecc_buf));

    // For multi-block (version 3-4 with multiple blocks): simplified single block
    // Works correctly for V1-V2 (1 block). V3-V4 multi-block simplified.
    uint8_t block_data = cap->data / cap->blocks;
    uint8_t block_ecc  = cap->ecc;
    uint8_t total_cw   = cap->data + (uint8_t)(cap->blocks * block_ecc);

    static uint8_t final_cw[200];
    memset(final_cw, 0, sizeof(final_cw));

    uint8_t fi = 0;
    for (uint8_t b = 0; b < cap->blocks; b++) {
        uint8_t d_start = b * block_data;
        uint8_t d_len   = (b == cap->blocks-1) ?
                          (cap->data - b*block_data) : block_data;
        for (uint8_t i = 0; i < d_len; i++) final_cw[fi++] = cw_buf[d_start+i];
    }
    for (uint8_t b = 0; b < cap->blocks; b++) {
        uint8_t d_start = b * block_data;
        uint8_t d_len   = (b == cap->blocks-1) ?
                          (cap->data - b*block_data) : block_data;
        rs_ecc(cw_buf + d_start, d_len, gen, gen_len, ecc_buf + b*block_ecc);
        for (uint8_t i = 0; i < block_ecc; i++) final_cw[fi++] = ecc_buf[b*block_ecc+i];
    }

    // ── 3. Place functional patterns ─────────────────────────────────────────
    place_finder(qrcode, 0, 0);
    place_finder(qrcode, (int16_t)(qrcode->size - 7), 0);
    place_finder(qrcode, 0, (int16_t)(qrcode->size - 7));
    place_timing(qrcode);
    place_dark(qrcode);
    if (version >= 2) {
        uint8_t ap = (uint8_t)(version * 4 + 10);
        place_alignment(qrcode, ap, ap);
    }

    // ── 4. Find best mask ─────────────────────────────────────────────────────
    uint8_t best_mask  = 0;
    int16_t best_score = 32767;

    for (uint8_t m = 0; m < 8; m++) {
        // Re-place functional patterns each attempt
        place_finder(qrcode, 0, 0);
        place_finder(qrcode, (int16_t)(qrcode->size - 7), 0);
        place_finder(qrcode, 0, (int16_t)(qrcode->size - 7));
        place_timing(qrcode);
        place_dark(qrcode);
        if (version >= 2) {
            uint8_t ap = (uint8_t)(version * 4 + 10);
            place_alignment(qrcode, ap, ap);
        }
        place_format(qrcode, m);
        place_data(qrcode, final_cw, (uint16_t)total_cw, m);

        int16_t s = score_mask(qrcode);
        if (s < best_score) { best_score = s; best_mask = m; }
    }

    // ── 5. Final placement with best mask ─────────────────────────────────────
    memset(dataBuffer, 0, buf_size);
    place_finder(qrcode, 0, 0);
    place_finder(qrcode, (int16_t)(qrcode->size - 7), 0);
    place_finder(qrcode, 0, (int16_t)(qrcode->size - 7));
    place_timing(qrcode);
    place_dark(qrcode);
    if (version >= 2) {
        uint8_t ap = (uint8_t)(version * 4 + 10);
        place_alignment(qrcode, ap, ap);
    }
    place_format(qrcode, best_mask);
    place_data(qrcode, final_cw, (uint16_t)total_cw, best_mask);

    qrcode->mask = best_mask;
    return 0;
}

bool qrcode_getModule(QRCode *qrcode, uint8_t x, uint8_t y)
{
    return get_module_raw(qrcode, x, y);
}
