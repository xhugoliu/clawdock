#pragma once

#include "touch_format.h"

typedef enum {
    SCREEN_AWAKE,
    SCREEN_WARNING,
    SCREEN_ASLEEP,
} screen_phase_t;

typedef struct {
    int64_t last_event_us;
    int stick_x, stick_y;
    uint8_t touch_reported, touch_valid;
    lab_touch_point_t touch_points[LAB_TOUCH_POINTS];
} screen_idle_t;

screen_phase_t screen_idle_update(screen_idle_t *idle, int64_t now_us, bool button_changed,
                                  bool stick_ready, int stick_x, int stick_y, const lab_touch_state_t *touch);
