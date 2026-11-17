#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "touch.h"
#include "app_manager.h"

typedef struct {
    bool buttons[4];
    int x;
    int y;
    bool adc_ok;
    bool joystick_calibrated;
    int normalized_x;
    int normalized_y;
    bool awake;
    bool sleep_warning;
    lab_touch_state_t touch;
    app_view_t app;
} lab_display_state_t;

esp_err_t lab_display_init(void);
esp_err_t lab_display_update(const lab_display_state_t *state);
esp_err_t lab_display_sleep_warning(void);
esp_err_t lab_display_set_awake(bool awake);
