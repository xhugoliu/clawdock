#include "touchpad.h"
#include "mouse.h"
#include <string.h>

#define TAP_MAX_MS 280
#define TAP_MOVE_LIMIT 12
#define SCROLL_STEP_PIXELS 24
#define MAX_SAMPLE_DELTA 40

void touchpad_reset(touchpad_t *pad) { memset(pad, 0, sizeof(*pad)); }

static int bound_delta(int n)
{
    if (n > MAX_SAMPLE_DELTA) return MAX_SAMPLE_DELTA;
    if (n < -MAX_SAMPLE_DELTA) return -MAX_SAMPLE_DELTA;
    return n;
}
static bool outside_tap_limit(int delta)
{
    return delta > TAP_MOVE_LIMIT || delta < -TAP_MOVE_LIMIT;
}
static int scroll_step(int delta, int *remainder)
{
    *remainder += delta;
    int result = *remainder / SCROLL_STEP_PIXELS;
    *remainder -= result * SCROLL_STEP_PIXELS;
    return result;
}
static void begin(touchpad_t *pad, const lab_touch_state_t *touch, unsigned fingers, int64_t now_ms)
{
    pad->fingers = fingers;
    pad->started_ms = now_ms;
    pad->tap_candidate = true;
    pad->id_a = touch->points[0].id;
    pad->id_b = fingers == 2 ? touch->points[1].id : 0;
    pad->start_ax = touch->points[0].x; pad->start_ay = touch->points[0].y;
    pad->start_bx = fingers == 2 ? touch->points[1].x : 0;
    pad->start_by = fingers == 2 ? touch->points[1].y : 0;
    pad->start_x = pad->last_x = fingers == 2 ? (touch->points[0].x + touch->points[1].x) / 2 : touch->points[0].x;
    pad->start_y = pad->last_y = fingers == 2 ? (touch->points[0].y + touch->points[1].y) / 2 : touch->points[0].y;
    pad->wheel_remainder = pad->pan_remainder = 0;
}

void touchpad_update(touchpad_t *pad, const lab_touch_state_t *touch,
                     int64_t now_ms, touchpad_output_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!touch->ready) {
        touchpad_reset(pad);
        pad->blocked = true; /* A read failure is not a finger release. */
        return;
    }
    if (touch->reported == 0) {
        if (!pad->blocked && pad->fingers && pad->tap_candidate &&
            now_ms >= pad->started_ms && now_ms - pad->started_ms <= TAP_MAX_MS)
            out->click = pad->fingers == 1 ? MOUSE_LEFT : MOUSE_RIGHT;
        touchpad_reset(pad);
        return;
    }
    if (pad->blocked) return;
    if (touch->reported == 2 && touch->valid == 2 && touch->points[0].id == touch->points[1].id) {
        pad->blocked = true;
        return;
    }
    if (touch->reported > 2) {
        touchpad_reset(pad);
        pad->blocked = true;
        return;
    }
    /* Some controllers report a lifted contact briefly before reducing count. */
    if (touch->valid != touch->reported) {
        if (touch->reported == 2 && pad->fingers == 1) {
            pad->fingers = 2;
            pad->tap_candidate = false;
        }
        return;
    }
    if (touch->reported == 1 && pad->fingers == 2) return; /* Wait for both to lift. */
    if (pad->fingers == 0 || (pad->fingers == 1 && touch->reported == 2)) {
        bool from_single = pad->fingers == 1;
        bool was_tap = pad->tap_candidate;
        int64_t started = pad->started_ms;
        begin(pad, touch, touch->reported, now_ms);
        if (from_single && (!was_tap || now_ms - started > TAP_MAX_MS)) pad->tap_candidate = false;
        return;
    }
    int x, y;
    if (pad->fingers == 1) {
        if (touch->points[0].id != pad->id_a) { pad->blocked = true; return; }
        x = touch->points[0].x; y = touch->points[0].y;
    } else {
        uint8_t a = touch->points[0].id, b = touch->points[1].id;
        if (!((a == pad->id_a && b == pad->id_b) || (a == pad->id_b && b == pad->id_a)) || a == b) {
            pad->blocked = true; return;
        }
        const lab_touch_point_t *pa = &touch->points[a == pad->id_a ? 0 : 1];
        const lab_touch_point_t *pb = &touch->points[a == pad->id_a ? 1 : 0];
        if (outside_tap_limit((int)pa->x - pad->start_ax) || outside_tap_limit((int)pa->y - pad->start_ay) ||
            outside_tap_limit((int)pb->x - pad->start_bx) || outside_tap_limit((int)pb->y - pad->start_by))
            pad->tap_candidate = false;
        x = (touch->points[0].x + touch->points[1].x) / 2;
        y = (touch->points[0].y + touch->points[1].y) / 2;
    }
    if (outside_tap_limit(x - pad->start_x) || outside_tap_limit(y - pad->start_y)) pad->tap_candidate = false;
    int dx = x - pad->last_x, dy = y - pad->last_y;
    pad->last_x = x; pad->last_y = y;
    if (dx > MAX_SAMPLE_DELTA || dx < -MAX_SAMPLE_DELTA || dy > MAX_SAMPLE_DELTA || dy < -MAX_SAMPLE_DELTA) {
        pad->tap_candidate = false; /* Reject coordinate discontinuities rather than jumping. */
        return;
    }
    dx = bound_delta(dx); dy = bound_delta(dy);
    if (pad->fingers == 1) {
        out->dx = (int8_t)(dx * 2);
        out->dy = (int8_t)(dy * 2);
    } else {
        out->scroll_x = -dx;
        out->scroll_y = -dy;
        out->pan = (int8_t)scroll_step(out->scroll_x, &pad->pan_remainder);
        out->wheel = (int8_t)scroll_step(out->scroll_y, &pad->wheel_remainder);
    }
}
