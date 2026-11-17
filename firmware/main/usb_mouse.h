#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t usb_mouse_init(void);
void usb_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);
