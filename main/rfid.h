#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Maximum length for the UID of an RFID card
#define RFID_UID_MAX_LEN  10

// Structure representing an RFID card and its Unique Identifier (UID)
typedef struct {
    uint8_t uid[RFID_UID_MAX_LEN]; // Array holding the UID bytes
    uint8_t uid_len;               // The length of the UID in bytes
} rfid_card_t;

// Initializes the MFRC522 SPI driver.
// Note that the SPI bus must be initialized using spi_bus_initialize() before calling this function.
// Returns ESP_OK if initialization is successful, otherwise returns the specific error code.
esp_err_t rfid_init(void);

// Polls the RFID reader to check if a card is present and reads its UID.
// The card parameter points to the structure where the read UID will be stored.
// Returns true if a card is successfully detected and read.
// Returns false if there is no card present or if a communication error happens.
bool rfid_poll(rfid_card_t *card);

// Converts the raw bytes of a card's UID into a readable hexadecimal string.
// For example, it converts raw bytes to a string like "A1B2C3D4".
// The string will be written into the provided buffer up to its specified size limit.
void rfid_uid_to_str(const rfid_card_t *card, char *buf, size_t buf_sz);

// Compares the UIDs of two RFID cards to check if they belong to the same physical card.
// Returns true if both UIDs are exactly identical in length and content, otherwise false.
bool rfid_uid_equal(const rfid_card_t *a, const rfid_card_t *b);
