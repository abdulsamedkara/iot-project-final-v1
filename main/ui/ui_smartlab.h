#pragma once
#include "display.h"

// Initializes all LVGL screens and their base styles.
// This function should be called only once during system initialization.
void ui_smartlab_init(void);

// Switches the active UI to the specified screen state and displays an optional message.
// Parameters:
//   id: The target screen state identifier.
//   msg: Additional message string to display on the screen (can be NULL).
void ui_smartlab_show(screen_id_t id, const char *msg);
