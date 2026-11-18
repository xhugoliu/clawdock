#include "screen_idle.h"
#include <stdlib.h>
#include <string.h>

#define SCREEN_IDLE_US (30LL * 60 * 1000000)
#define SCREEN_WARNING_US (60LL * 1000000)
#define STICK_MOVEMENT 30
#define TOUCH_MOVEMENT 3

static bool stick_event(screen_idle_t *idle, bool ready, int x, int y)
{
    if (!ready || (abs(x - idle->stick_x) < STICK_MOVEMENT && abs(y - idle->stick_y) < STICK_MOVEMENT))
        return false;
    idle->stick_x = x;
    idle->stick_y = y;
    return true;
}

static bool touch_changed(const screen_idle_t *idle, const lab_touch_state_t *touch)
{
    if (idle->touch_reported != touch->reported || idle->touch_valid != touch->valid) return true;
    for (unsigned i = 0; i < touch->valid; ++i) {
        const lab_touch_point_t *point = &touch->points[i];
        bool unchanged = false;
        for (unsigned j = 0; j < idle->touch_valid; ++j) {
            const lab_touch_point_t *previous = &idle->touch_points[j];
            if (point->id == previous->id && abs((int)point->x - previous->x) < TOUCH_MOVEMENT &&
                abs((int)point->y - previous->y) < TOUCH_MOVEMENT) {
                unchanged = true;
                break;
            }
        }
        if (!unchanged) return true;
    }
    return false;
}

static bool touch_event(screen_idle_t *idle, const lab_touch_state_t *touch)
{
    if (!touch->ready || !touch_changed(idle, touch)) return false;
    idle->touch_reported = touch->reported;
    idle->touch_valid = touch->valid;
    memcpy(idle->touch_points, touch->points, touch->valid * sizeof(touch->points[0]));
    return true;
}

screen_phase_t screen_idle_update(screen_idle_t *idle, int64_t now_us, bool button_changed,
                                  bool stick_ready, int stick_x, int stick_y, const lab_touch_state_t *touch)
{
    /* Compare with the last input event so small movements can accumulate without counting sensor noise. */
    bool stick_moved = stick_event(idle, stick_ready, stick_x, stick_y);
    bool touch_moved = touch_event(idle, touch);
    if (button_changed || stick_moved || touch_moved) idle->last_event_us = now_us;
    int64_t elapsed = now_us - idle->last_event_us;
    if (elapsed >= SCREEN_IDLE_US) return SCREEN_ASLEEP;
    if (elapsed >= SCREEN_IDLE_US - SCREEN_WARNING_US) return SCREEN_WARNING;
    return SCREEN_AWAKE;
}
