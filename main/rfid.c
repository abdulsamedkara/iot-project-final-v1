// MFRC522 RFID reader driver
// This driver uses the ESP-IDF SPI master API to communicate with the RFID module.
// The SPI bus itself is expected to be initialized elsewhere in the project (usually in main.c).
// This file is responsible for adding the MFRC522 device to the SPI bus and handling its commands.

#include "rfid.h"
#include "config.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "rfid";

// Handle for the SPI device used to communicate with the RFID reader
static spi_device_handle_t s_spi = NULL;

// MFRC522 Register Addresses
// These are the internal memory addresses of the MFRC522 used to configure and control it.
#define REG_COMMAND         0x01
#define REG_COM_IEN         0x02
#define REG_COM_IRQ         0x04
#define REG_ERROR           0x06
#define REG_FIFO_DATA       0x09
#define REG_FIFO_LEVEL      0x0A
#define REG_CONTROL         0x0C
#define REG_BIT_FRAMING     0x0D
#define REG_COLL            0x0E
#define REG_MODE            0x11
#define REG_TX_CONTROL      0x14
#define REG_TX_ASK          0x15
#define REG_RX_GAIN         0x26
#define REG_CRC_RESULT_MSB  0x21
#define REG_CRC_RESULT_LSB  0x22
#define REG_T_MODE          0x2A
#define REG_T_PRESCALER     0x2B
#define REG_T_RELOAD_H      0x2C
#define REG_T_RELOAD_L      0x2D
#define REG_VERSION         0x37

// MFRC522 Commands
// Commands that instruct the MFRC522 to perform specific internal operations.
#define CMD_IDLE            0x00
#define CMD_CALC_CRC        0x03
#define CMD_TRANSCEIVE      0x0C
#define CMD_SOFT_RESET      0x0F

// ISO 14443A PICC Commands
// These commands are sent over the air to the PICC (Proximity Integrated Circuit Card).
#define PICC_REQA           0x26
#define PICC_ANTICOLL       0x93
#define PICC_HLTA           0x50

// SPI Read and Write Interface
// The MFRC522 uses an 8-bit format for addressing registers over SPI.
// The most significant bit (bit 7) determines the operation: 0 for write, 1 for read.
// The address occupies bits 1 to 6, and bit 0 is always 0.

// Writes a specific value to an MFRC522 register via SPI.
// It constructs the 2-byte transaction buffer containing the register address and the value to write.
static void mfrc_write(uint8_t reg, uint8_t val)
{
    // Shift the register address left by 1 and mask it to keep bits 1 to 6.
    // The MSB is 0 because this is a write operation.
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), val };
    spi_transaction_t t = {
        .length    = 16, // Length is in bits
        .tx_buffer = tx,
    };
    spi_device_transmit(s_spi, &t);
}

// Reads a value from an MFRC522 register via SPI.
// It sends the register address and reads the subsequent byte returned by the device.
static uint8_t mfrc_read(uint8_t reg)
{
    // Shift the register address and set the MSB to 1 for a read operation.
    uint8_t tx[2] = { (uint8_t)(((reg << 1) & 0x7E) | 0x80), 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_transmit(s_spi, &t);
    return rx[1]; // The valid data is in the second byte received
}

// Sets specific bits in a register without modifying the others.
// It reads the current value, applies a bitwise OR with the mask, and writes it back.
static void mfrc_set_bits(uint8_t reg, uint8_t mask)
{
    mfrc_write(reg, mfrc_read(reg) | mask);
}

// Clears specific bits in a register without modifying the others.
// It reads the current value, applies a bitwise AND with the inverted mask, and writes it back.
static void mfrc_clear_bits(uint8_t reg, uint8_t mask)
{
    mfrc_write(reg, mfrc_read(reg) & ~mask);
}

// MFRC522 Initialization

// Issues a soft reset command to the MFRC522 to bring it to its default state.
// We must wait briefly afterward to allow the reset to complete.
static void mfrc_reset(void)
{
    mfrc_write(REG_COMMAND, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// Turns on the antenna of the MFRC522 so it can communicate with cards.
// It checks the transmission control register and enables the driver pins if they aren't already.
static void mfrc_antenna_on(void)
{
    uint8_t val = mfrc_read(REG_TX_CONTROL);
    if ((val & 0x03) != 0x03) {
        mfrc_set_bits(REG_TX_CONTROL, 0x03);
    }
}

// Initializes the RFID module by adding the SPI device and applying the optimal configurations.
// Returns ESP_OK if it connects and verifies the device version successfully.
esp_err_t rfid_init(void)
{
    // Add the RFID SPI device to the existing bus if it hasn't been added yet
    if (s_spi == NULL) {
        spi_device_interface_config_t dev_cfg = {
            .clock_speed_hz = RFID_SPI_FREQ_HZ,
            .mode           = 0,    // Standard SPI mode 0 (CPOL=0, CPHA=0)
            .spics_io_num   = RFID_CS_GPIO,
            .queue_size     = 4,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &dev_cfg, &s_spi));
    }

    // Reset the module to clear any weird states
    mfrc_reset();

    // Configure the internal timer to establish a strict communication timeout (around 25ms).
    // This prevents the system from hanging if a card disappears midway.
    mfrc_write(REG_T_MODE,      0x8D);
    mfrc_write(REG_T_PRESCALER, 0x3E);
    mfrc_write(REG_T_RELOAD_H,  0x00);
    mfrc_write(REG_T_RELOAD_L,  0x1E);

    // Apply standard modulation presets and maximize the antenna receiver gain.
    // Maximizing the gain (0x70 means 48dB) significantly improves reading stability, especially for third-party tags.
    mfrc_write(REG_TX_ASK,  0x40);
    mfrc_write(REG_MODE,    0x3D);
    mfrc_write(REG_RX_GAIN, 0x70);

    // Power up the antenna
    mfrc_antenna_on();

    // Verify the version to ensure the MFRC522 is wired correctly and responsive.
    uint8_t ver = mfrc_read(REG_VERSION);
    if (ver == 0x91 || ver == 0x92 || ver == 0x18 || ver == 0x88) {
        ESP_LOGI(TAG, "MFRC522 ready — Version: 0x%02X", ver);
    } else {
        ESP_LOGW(TAG, "MFRC522 unexpected version: 0x%02X — check hardware connection", ver);
    }
    return ESP_OK;
}

// PICC Communication

// A helper structure to hold the response from a card communication attempt.
typedef struct {
    uint8_t data[16]; // Buffer for received data
    uint8_t len;      // Amount of data received
    bool    valid;    // Whether the transaction was successful
} mfrc_resp_t;

// Sends raw bytes to the card and waits for its response.
// The last_bits parameter handles cases where the transmission does not align to full bytes (like REQA).
static mfrc_resp_t mfrc_transceive(const uint8_t *tx_data, uint8_t tx_len,
                                    uint8_t last_bits)
{
    mfrc_resp_t resp = {0};

    // Enable interrupts for the end of transmission and reception, and clear the FIFO buffer.
    mfrc_write(REG_COM_IEN,    0xF7);
    mfrc_write(REG_COM_IRQ,    0x7F);
    mfrc_write(REG_FIFO_LEVEL, 0x80);
    
    // Stop any ongoing command
    mfrc_write(REG_COMMAND,    CMD_IDLE);

    // Load the transmission data into the module's FIFO buffer
    for (uint8_t i = 0; i < tx_len; i++) {
        mfrc_write(REG_FIFO_DATA, tx_data[i]);
    }

    // Configure the framing (partial bytes) and start the transceive command
    mfrc_write(REG_BIT_FRAMING, last_bits);
    mfrc_write(REG_COMMAND, CMD_TRANSCEIVE);
    
    // Tell the module to actually start pushing the data over the antenna
    mfrc_set_bits(REG_BIT_FRAMING, 0x80);

    // Wait for the interrupt flags indicating the operation has finished or timed out.
    // We use a simple polling loop with a short delay.
    uint16_t timeout = 50;
    uint8_t irq;
    do {
        irq = mfrc_read(REG_COM_IRQ);
        timeout--;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (!(irq & 0x31) && timeout);

    // Stop transmission
    mfrc_clear_bits(REG_BIT_FRAMING, 0x80);

    if (!timeout) {
        ESP_LOGD(TAG, "Transceive timeout");
        return resp;
    }

    // Check for any protocol errors
    uint8_t err = mfrc_read(REG_ERROR);
    if (err & 0x1B) {
        ESP_LOGD(TAG, "Transceive error: 0x%02X", err);
        return resp;
    }

    // Read the response from the FIFO buffer
    uint8_t fifo_len = mfrc_read(REG_FIFO_LEVEL);
    if (fifo_len > sizeof(resp.data)) fifo_len = sizeof(resp.data);

    for (uint8_t i = 0; i < fifo_len; i++) {
        resp.data[i] = mfrc_read(REG_FIFO_DATA);
    }
    
    resp.len   = fifo_len;
    resp.valid = true;
    return resp;
}

// Card Detection and UID Reading

// Checks if a card is present near the antenna and attempts to read its UID.
// Returns true if a card was successfully read.
bool rfid_poll(rfid_card_t *card)
{
    // Step 1: Send the REQA (Request Type A) command.
    // This wakes up any cards in the field. REQA is a 7-bit command.
    uint8_t reqa = PICC_REQA;
    mfrc_resp_t atqa = mfrc_transceive(&reqa, 1, 7);
    if (!atqa.valid || atqa.len < 2) return false;

    // Step 2: Send the Anti-Collision command.
    // If multiple cards are present, this helps single one out. We use it to get the raw UID bytes.
    uint8_t anticoll[2] = {PICC_ANTICOLL, 0x20};
    mfrc_resp_t uid_resp = mfrc_transceive(anticoll, 2, 0);
    if (!uid_resp.valid || uid_resp.len < 5) return false;

    // Verify the integrity of the UID using the BCC (Block Check Character).
    // The 5th byte is the XOR checksum of the first 4 bytes.
    uint8_t bcc = 0;
    for (int i = 0; i < 4; i++) bcc ^= uid_resp.data[i];
    if (bcc != uid_resp.data[4]) {
        ESP_LOGD(TAG, "UID BCC error");
        return false;
    }

    // Handle a specific edge case: hardware noise can sometimes cause the reader to see all zeroes.
    // An all-zero UID will have a BCC of zero, meaning the checksum check will accidentally pass.
    // We must manually reject completely empty UIDs.
    if (uid_resp.data[0] == 0 && uid_resp.data[1] == 0 && 
        uid_resp.data[2] == 0 && uid_resp.data[3] == 0) {
        return false;
    }

    // Copy the verified UID into the user's struct
    memcpy(card->uid, uid_resp.data, 4);
    card->uid_len = 4;

    // Step 3: Send the HLTA (Halt A) command.
    // This puts the card back to sleep so it doesn't repeatedly spam the reader on every polling cycle.
    uint8_t hlta[2] = {PICC_HLTA, 0x00};
    mfrc_transceive(hlta, 2, 0);

    return true;
}

// Converts a card's binary UID into a human-readable hexadecimal string.
void rfid_uid_to_str(const rfid_card_t *card, char *buf, size_t buf_sz)
{
    buf[0] = '\0';
    for (uint8_t i = 0; i < card->uid_len && (i * 2 + 2) < buf_sz; i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X", card->uid[i]);
        strcat(buf, hex);
    }
}

// Compares two cards to see if their UIDs are exactly the same.
bool rfid_uid_equal(const rfid_card_t *a, const rfid_card_t *b)
{
    if (a->uid_len != b->uid_len) return false;
    return memcmp(a->uid, b->uid, a->uid_len) == 0;
}
