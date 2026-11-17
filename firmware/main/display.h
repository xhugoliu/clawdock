#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "touch.h"

typedef struct {
    bool buttons[4];
    int x;
    int y;
    bool adc_ok;
    lab_touch_state_t touch;
} lab_display_state_t;

esp_err_t lab_display_init(void);
esp_err_t lab_display_update(const lab_display_state_t *state);
