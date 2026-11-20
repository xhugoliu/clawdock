#pragma once
#include <stdint.h>
#include "display.h"

void app_ui_sleep_warning(uint16_t *buffer);
/* Host previews use the original palette. */
void app_ui_render(uint16_t *buffer, const lab_display_state_t *state);
void app_ui_render_for_panel(uint16_t *buffer, const lab_display_state_t *state);
