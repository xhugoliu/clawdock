#pragma once

#include "codex_monitor.h"
#include "touch_format.h"

typedef enum {
    AGENTS_SCREEN_KEEP_AWAKE,
    AGENTS_SCREEN_WAITING_FOR_NAP,
    AGENTS_SCREEN_NAPPING,
} agents_screen_mode_t;

typedef struct {
    int64_t idle_since_ms;
    agents_screen_mode_t mode;
    bool asleep;
    int stick_x, stick_y;
    uint8_t touch_reported, touch_valid;
    lab_touch_point_t touch_points[LAB_TOUCH_POINTS];
} agents_screen_t;

agents_screen_mode_t agents_screen_mode(const codex_summary_t *sessions, bool napping, int64_t now_ms);
bool agents_screen_update(agents_screen_t *screen, agents_screen_mode_t mode, int64_t now_ms,
                          bool button_changed, bool stick_ready, int stick_x, int stick_y,
                          const lab_touch_state_t *touch);
