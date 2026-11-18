#include <assert.h>
#include <stdio.h>
#include "screen_idle.h"

#define MINUTE_US (60LL * 1000000)

static void test_idle_deadlines(void)
{
    screen_idle_t idle = { .last_event_us = MINUTE_US };
    lab_touch_state_t touch = { .ready = true };
    assert(screen_idle_update(&idle, 30 * MINUTE_US - 1, false, true, 0, 0, &touch) == SCREEN_AWAKE);
    assert(screen_idle_update(&idle, 30 * MINUTE_US, false, true, 0, 0, &touch) == SCREEN_WARNING);
    assert(screen_idle_update(&idle, 31 * MINUTE_US - 1, false, true, 0, 0, &touch) == SCREEN_WARNING);
    assert(screen_idle_update(&idle, 31 * MINUTE_US, false, true, 0, 0, &touch) == SCREEN_ASLEEP);
    assert(screen_idle_update(&idle, 32 * MINUTE_US, true, true, 0, 0, &touch) == SCREEN_AWAKE);
    assert(screen_idle_update(&idle, 61 * MINUTE_US, false, true, 0, 0, &touch) == SCREEN_WARNING);
    assert(screen_idle_update(&idle, 61 * MINUTE_US + 1, true, true, 0, 0, &touch) == SCREEN_AWAKE);
}

static void test_stick_movement(void)
{
    screen_idle_t idle = {0};
    lab_touch_state_t touch = {0};
    assert(screen_idle_update(&idle, MINUTE_US, false, true, 500, 0, &touch) == SCREEN_AWAKE);
    assert(screen_idle_update(&idle, 31 * MINUTE_US, false, true, 502, 0, &touch) == SCREEN_ASLEEP);
    assert(screen_idle_update(&idle, 31 * MINUTE_US + 1, false, false, 0, 0, &touch) == SCREEN_ASLEEP);
    assert(screen_idle_update(&idle, 31 * MINUTE_US + 2, false, true, 529, 0, &touch) == SCREEN_ASLEEP);
    assert(screen_idle_update(&idle, 31 * MINUTE_US + 3, false, true, 530, 0, &touch) == SCREEN_AWAKE);
    assert(screen_idle_update(&idle, 61 * MINUTE_US + 3, false, true, 0, 0, &touch) == SCREEN_AWAKE);
}

static void test_touch_events(void)
{
    screen_idle_t idle = {0};
    lab_touch_state_t touch = {
        .ready = true, .reported = 2, .valid = 2,
        .points = {{ .x = 40, .y = 50, .id = 1 }, { .x = 60, .y = 70, .id = 2 }},
    };
    assert(screen_idle_update(&idle, 31 * MINUTE_US, false, true, 0, 0, &touch) == SCREEN_AWAKE);
    lab_touch_point_t first = touch.points[0];
    touch.points[0] = touch.points[1];
    touch.points[1] = first;
    ++touch.points[1].x;
    assert(screen_idle_update(&idle, 60 * MINUTE_US, false, true, 0, 0, &touch) == SCREEN_WARNING);
    lab_touch_state_t failed = {0};
    assert(screen_idle_update(&idle, 61 * MINUTE_US, false, true, 0, 0, &failed) == SCREEN_ASLEEP);
    assert(screen_idle_update(&idle, 61 * MINUTE_US + 1, false, true, 0, 0, &touch) == SCREEN_ASLEEP);
    touch.points[1].x += 2;
    assert(screen_idle_update(&idle, 61 * MINUTE_US + 2, false, true, 0, 0, &touch) == SCREEN_AWAKE);
    touch.reported = touch.valid = 0;
    assert(screen_idle_update(&idle, 91 * MINUTE_US + 2, false, true, 0, 0, &touch) == SCREEN_AWAKE);
}

int main(void)
{
    test_idle_deadlines();
    test_stick_movement();
    test_touch_events();
    puts("screen idle tests passed");
}
