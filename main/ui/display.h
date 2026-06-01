#pragma once
#include "esp_err.h"
#include <stdbool.h>

// SmartLab screen states enumeration
typedef enum {
    SCREEN_IDLE = 0,      // Idle state, waiting for RFID card swipe
    SCREEN_RFID_READ,     // Card successfully read, welcoming the user
    SCREEN_READY,         // System is ready, waiting for Push-to-Talk (PTT) action
    SCREEN_RECORDING,     // Microphone is active and listening to user audio
    SCREEN_PROCESSING,    // Audio sent to server, waiting for AI response
    SCREEN_SPEAKING,      // Playing the received AI response audio through speaker
    SCREEN_SMOKE_ALERT,   // Critical alert state triggered by smoke detection
    SCREEN_ERROR,         // System error state displaying error messages
} screen_id_t;

// Initializes LVGL and the ILI9341 display hardware.
esp_err_t display_init(void);

// Switches the current screen state to a new one.
// Parameters:
//   id: The new screen state to switch to.
//   msg: An optional sub-message string to display (can be NULL).
void display_switch(screen_id_t id, const char *msg);

// Acquires the LVGL mutex.
// This must be called before making any updates to the UI from a different task.
// Parameters:
//   timeout_ms: Maximum time to wait for the mutex in milliseconds.
// Returns true if the mutex was successfully acquired, false otherwise.
bool display_lock(int timeout_ms);

// Releases the previously acquired LVGL mutex.
// This must be called after finishing UI updates.
void display_unlock(void);
