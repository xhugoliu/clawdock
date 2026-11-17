#include <assert.h>
#include <stdio.h>
#include "app_manager.h"

static app_manager_t manager;
static app_input_t input;
static int64_t now;

static void update(int64_t delta)
{
    now += delta;
    app_manager_update(&manager, &input, now);
}

static void reset(void)
{
    app_manager_init(&manager);
    input = (app_input_t){ .joystick_ready = true, .touch_available = true, .touch_ready = true };
    now = 0;
    update(0);
}

static void press_chord(void)
{
    input.buttons[0] = true; update(20);
    input.buttons[1] = true; update(20);
    input.buttons[2] = true; update(20);
}

static void open_selector(void)
{
    press_chord();
    assert(manager.view.choosing);
}

static void release_chord(void)
{
    input.buttons[0] = input.buttons[1] = input.buttons[2] = false;
    update(20);
}

static void tap_sw(unsigned button)
{
    input.buttons[button] = true; update(20);
    input.buttons[button] = false; update(20);
}

static void move(int y)
{
    input.joystick_y = y; update(20);
    input.joystick_y = 0; update(20);
}

int main(void)
{
    reset();
    open_selector();
    release_chord(); assert(!manager.view.choosing && manager.view.current == 0);
    update(20);
    open_selector();
    assert(manager.view.current == 0 && manager.view.selected == 0);
    update(1000); assert(manager.view.choosing); /* Long hold never toggles. */
    move(800); assert(manager.view.selected == JINTAI_APP_COUNT - 1); /* Wrap up. */
    move(-800); assert(manager.view.selected == 0); /* Wrap down. */
    move(-800); assert(manager.view.selected == 1);
    input.joystick_y = -800; update(20);
    assert(manager.view.selected == 2);
    update(500); assert(manager.view.selected == 2); /* One step until centered. */
    input.joystick_y = 0; update(20);
    assert(manager.view.current == 0); /* No switch while held. */
    input.buttons[0] = false; update(20);
    input.buttons[1] = false; update(20);
    assert(manager.view.choosing && manager.view.current == 0); /* Wait for all up. */
    input.buttons[2] = false; update(20);
    assert(!manager.view.choosing && manager.view.current == 2 && manager.view.presses == 0);
    update(20); /* Guard releases only after inputs return to neutral. */
    tap_sw(0); assert(manager.view.presses == 1);

    open_selector();
    assert(manager.view.presses == 1 && manager.view.selected == 2);
    release_chord(); /* Selecting the original app resumes its state. */
    assert(manager.view.current == 2 && manager.view.presses == 1);
    update(20);
    open_selector(); move(-800);
    release_chord();
    assert(manager.view.current == 3 && manager.view.presses == 0);
    update(20); tap_sw(1); assert(manager.view.presses == 1);

    /* Even a short chord is reserved and does not leak into the app. */
    press_chord(); assert(manager.view.choosing);
    release_chord();
    assert(!manager.view.choosing && manager.view.presses == 1);
    update(20);
    open_selector();
    input.joystick_y = 800; update(20); /* Keep stick deflected after release. */
    release_chord();
    assert(manager.view.current == 2 && manager.view.presses == 0 && manager.input_blocked);
    input.buttons[0] = true; update(20);
    assert(manager.view.presses == 0);
    input.buttons[0] = false; input.joystick_y = 0; update(20);
    assert(!manager.input_blocked);

    /* A late third key cannot turn already delivered events into a chord. */
    input.buttons[0] = true; update(20); update(181);
    input.buttons[1] = true; update(20);
    input.buttons[2] = true; update(20); update(1000);
    assert(!manager.view.choosing && manager.view.presses == 3);
    release_chord();

    app_manager_wake(&manager);
    press_chord(); update(1000);
    assert(!manager.view.choosing);
    release_chord();
    assert(!manager.input_blocked);
    open_selector();
    puts("app manager tests passed");
}
