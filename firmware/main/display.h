#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t lab_display_init(void);
esp_err_t lab_display_update(const bool buttons[4], int x, int y, bool adc_ok);
