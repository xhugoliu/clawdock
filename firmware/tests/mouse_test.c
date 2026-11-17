#include <assert.h>
#include <stdio.h>
#include "app_manager.h"

static app_manager_t app;
static app_input_t input;
static int64_t now;
static void update(int delta) { now += delta; app_manager_update(&app, &input, now); }
static void set(unsigned i, bool down) { input.buttons[i] = down; update(20); }
static void clear_events(void) { uint8_t b; while (mouse_peek(&app.runtime.mouse, &b)) mouse_pop(&app.runtime.mouse); }
static void expect(uint8_t expected) { uint8_t b; assert(mouse_peek(&app.runtime.mouse, &b) && b == expected); mouse_pop(&app.runtime.mouse); }
static void none(void) { uint8_t b; assert(!mouse_peek(&app.runtime.mouse, &b)); }
static void start_mouse(void) {
    app_manager_init(&app);
    input = (app_input_t){ .joystick_ready = true };
    now = 0;
    set(0, true); set(1, true); set(2, true);
    input.joystick_y = -800; update(20);
    input.joystick_y = 0; input.buttons[0] = input.buttons[1] = input.buttons[2] = false; update(20); update(20);
    assert(app.view.current == 1 && !app.input_blocked);
}
int main(void) {
    start_mouse();
    set(0, true); set(0, false); assert(app.view.mouse_scroll); none();
    set(1, true); update(181); expect(MOUSE_RIGHT); set(1, false); expect(0); none();
    set(2, true); set(2, false); expect(MOUSE_LEFT); expect(0); none();
    set(0, true); set(1, true); update(181); expect(MOUSE_BACK);
    set(0, false); none(); set(1, false); expect(0); assert(app.view.mouse_scroll);
    set(2, true); set(1, true); set(2, false); set(1, false); expect(MOUSE_FORWARD); expect(0); none();
    /* Third key within the window always opens the selector without mouse output. */
    set(0, true); set(1, true); set(2, true); assert(app.view.choosing); none();
    input.buttons[0] = input.buttons[1] = input.buttons[2] = false; update(20); update(20);
    assert(app.view.current == 1 && app.view.mouse_scroll);
    /* Wake releases a held mouse button and blocks inputs until neutral. */
    set(2, true); update(181); expect(MOUSE_LEFT); app_manager_wake(&app); expect(0); set(2, false); update(20);
    clear_events();
    int8_t dx, dy, wheel, pan;
    mouse_t mouse = {0};
    mouse_motion(&mouse, 1000, 1000, &dx, &dy, &wheel, &pan);
    assert(dx == 16 && dy == -16 && wheel == 0 && pan == 0);
    mouse_button(&mouse, 0, true);
    int sum = 0;
    for (int i = 0; i < 50; ++i) { mouse_motion(&mouse, 0, 1000, &dx, &dy, &wheel, &pan); sum += wheel; }
    assert(sum == 8 && dx == 0 && dy == 0 && pan == 0);
    puts("mouse tests passed");
}
