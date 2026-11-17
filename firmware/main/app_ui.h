#pragma once
#include <stdint.h>
#include "display.h"

void app_ui_sleep_warning(uint16_t *buffer);
void app_ui_render(uint16_t *buffer, const lab_display_state_t *state);
