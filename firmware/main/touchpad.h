#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "touch_format.h"

typedef struct {
    unsigned fingers;
    bool blocked;
    bool tap_candidate;
    int start_x, start_y;
    int start_ax, start_ay, start_bx, start_by;
    int last_x, last_y;
    uint8_t id_a, id_b;
    int64_t started_ms;
    int wheel_remainder, pan_remainder;
} touchpad_t;

typedef struct {
    int8_t dx, dy;
    int8_t wheel, pan;
    int scroll_x, scroll_y;
    uint8_t click;
} touchpad_output_t;

void touchpad_reset(touchpad_t *pad);
void touchpad_update(touchpad_t *pad, const lab_touch_state_t *touch,
                     int64_t now_ms, touchpad_output_t *out);
