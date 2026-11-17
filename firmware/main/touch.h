#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#include "touch_format.h"

esp_err_t lab_touch_init(void);
esp_err_t lab_touch_read(lab_touch_state_t *state);
