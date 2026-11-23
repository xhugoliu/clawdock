#include "agents_screen.h"
#include <stdlib.h>
#include <string.h>

#define NAP_TIMEOUT_MS (10LL * 60 * 1000)
#define STICK_MOVEMENT 30
#define TOUCH_MOVEMENT 3

static bool stick_event(agents_screen_t *screen, bool ready, int x, int y)
{
    if (!ready || (abs(x - screen->stick_x) < STICK_MOVEMENT && abs(y - screen->stick_y) < STICK_MOVEMENT))
        return false;
    screen->stick_x = x;
    screen->stick_y = y;
    return true;
}

static bool touch_changed(const agents_screen_t *screen, const lab_touch_state_t *touch)
{
    if (screen->touch_reported != touch->reported || screen->touch_valid != touch->valid) return true;
    for (unsigned i = 0; i < touch->valid; ++i) {
        const lab_touch_point_t *point = &touch->points[i];
        bool unchanged = false;
        for (unsigned j = 0; j < screen->touch_valid; ++j) {
            const lab_touch_point_t *previous = &screen->touch_points[j];
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

static bool touch_event(agents_screen_t *screen, const lab_touch_state_t *touch)
{
    if (!touch->ready || !touch_changed(screen, touch)) return false;
    screen->touch_reported = touch->reported;
    screen->touch_valid = touch->valid;
    memcpy(screen->touch_points, touch->points, touch->valid * sizeof(touch->points[0]));
    return true;
}

agents_screen_mode_t agents_screen_mode(const codex_summary_t *sessions, bool napping, int64_t now_ms)
{
    if (codex_summary_is_current(sessions, now_ms) && memchr(sessions->states, CODEX_RUNNING, sessions->count))
        return AGENTS_SCREEN_KEEP_AWAKE;
    return napping ? AGENTS_SCREEN_NAPPING : AGENTS_SCREEN_WAITING_FOR_NAP;
}

bool agents_screen_update(agents_screen_t *screen, agents_screen_mode_t mode, int64_t now_ms,
                          bool button_changed, bool stick_ready, int stick_x, int stick_y,
                          const lab_touch_state_t *touch)
{
    /* Compare with the last input event so small movements can accumulate without counting sensor noise. */
    bool stick_moved = stick_event(screen, stick_ready, stick_x, stick_y);
    bool touch_moved = touch_event(screen, touch);
    bool activity = button_changed || stick_moved || touch_moved;
    if (mode != screen->mode || activity) screen->idle_since_ms = now_ms;
    screen->mode = mode;

    int64_t idle_ms = now_ms - screen->idle_since_ms;
    if (mode == AGENTS_SCREEN_KEEP_AWAKE || activity) screen->asleep = false;
    else if (mode == AGENTS_SCREEN_NAPPING && idle_ms >= NAP_TIMEOUT_MS) screen->asleep = true;
    return !screen->asleep;
}
