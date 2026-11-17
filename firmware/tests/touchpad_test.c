#include <assert.h>
#include <stdio.h>
#include "touchpad.h"
#include "mouse.h"

static touchpad_t pad;
static lab_touch_state_t touch = { .ready = true };
static touchpad_output_t out;
static int64_t now;
static void sample(unsigned count, unsigned valid, int x1, int y1, int x2, int y2) {
    touch.reported = count; touch.valid = valid;
    touch.points[0] = (lab_touch_point_t){ .x = x1, .y = y1, .id = 0 };
    touch.points[1] = (lab_touch_point_t){ .x = x2, .y = y2, .id = 1 };
    now += 20;
    touchpad_update(&pad, &touch, now, &out);
}
static void reset(void) { touchpad_reset(&pad); touch.ready = true; now = 0; }
int main(void) {
    reset(); sample(1, 1, 100, 100, 0, 0); assert(!out.click && !out.dx);
    sample(0, 0, 0, 0, 0, 0); assert(out.click == MOUSE_LEFT);
    reset(); sample(1, 1, 100, 100, 0, 0); sample(1, 1, 112, 105, 0, 0);
    assert(out.dx == 24 && out.dy == 10); sample(1, 1, 130, 105, 0, 0);
    sample(0, 0, 0, 0, 0, 0); assert(!out.click);
    reset(); sample(1, 1, 100, 100, 0, 0); sample(2, 2, 100, 100, 200, 100);
    sample(1, 1, 100, 100, 0, 0); assert(!out.dx && !out.click);
    sample(0, 0, 0, 0, 0, 0); assert(out.click == MOUSE_RIGHT);
    reset(); sample(2, 2, 100, 100, 200, 100); sample(2, 2, 112, 112, 212, 112);
    assert(out.scroll_x == -12 && out.scroll_y == -12 && !out.wheel);
    sample(2, 2, 124, 124, 224, 124); assert(out.wheel == -1 && out.pan == -1);
    sample(1, 1, 124, 124, 0, 0); assert(!out.dx && !out.click);
    sample(0, 0, 0, 0, 0, 0); assert(!out.click);
    reset(); sample(2, 2, 100, 100, 200, 100); sample(2, 2, 80, 100, 220, 100);
    sample(0, 0, 0, 0, 0, 0); assert(!out.click); /* Pinch is not a right click. */
    reset(); sample(1, 1, 100, 100, 0, 0); touch.ready = false;
    touchpad_update(&pad, &touch, now + 20, &out); touch.ready = true;
    sample(0, 0, 0, 0, 0, 0); assert(!out.click); /* Error is not a tap. */
    reset(); sample(1, 1, 100, 100, 0, 0); sample(1, 1, 300, 100, 0, 0);
    assert(!out.dx); sample(0, 0, 0, 0, 0, 0); assert(!out.click);
    reset(); sample(1, 1, 100, 100, 0, 0); sample(2, 1, 100, 100, 0, 0);
    sample(1, 1, 200, 200, 0, 0); assert(!out.dx && !out.click);
    sample(0, 0, 0, 0, 0, 0); assert(!out.click); /* Incomplete two-finger report. */
    reset(); sample(2, 2, 100, 100, 200, 100);
    touch.points[0] = (lab_touch_point_t){ .x = 205, .y = 110, .id = 1 };
    touch.points[1] = (lab_touch_point_t){ .x = 105, .y = 110, .id = 0 };
    touchpad_update(&pad, &touch, now + 20, &out);
    assert(out.scroll_x == -5 && out.scroll_y == -10); /* Contact order may swap. */
    reset(); sample(1, 1, 100, 100, 0, 0); now += 300;
    sample(0, 0, 0, 0, 0, 0); assert(!out.click);
    puts("touchpad tests passed");
}
