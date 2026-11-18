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

static bool action_pose(clawd_pose_t pose)
{
    return pose == CLAWD_HAPPY || (pose >= CLAWD_GROOVING && pose < CLAWD_POSE_COUNT);
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
    assert(!manager.view.choosing && manager.view.current == JINTAI_CODEX_APP);
    codex_summary_t sessions = { .count = 3, .ids = {{1}, {2}, {3}} };
    input.codex = &sessions;
    update(20); /* Guard releases only after inputs return to neutral. */
    update(20); assert(manager.view.codex_row == 0);
    input.joystick_y = -800; update(20);
    assert(manager.view.codex_row == 1 && manager.view.codex_id[0] == 2);
    update(500); assert(manager.view.codex_row == 1);
    input.joystick_y = 0; update(20);
    sessions.ids[0][0] = 2;
    sessions.ids[1][0] = 3;
    sessions.count = 2;
    update(20); assert(manager.view.codex_row == 0 && manager.view.codex_id[0] == 2);
    move(-800); assert(manager.view.codex_row == 1 && manager.view.codex_id[0] == 3);
    move(-800); assert(manager.view.codex_row == 0 && manager.view.codex_id[0] == 2); /* Wrap down. */
    move(800); assert(manager.view.codex_row == 1 && manager.view.codex_id[0] == 3); /* Wrap up. */

    tap_sw(1);
    assert(manager.view.codex_detail && manager.view.codex_page == 0 && manager.view.codex_id[0] == 3);
    move(-800); assert(manager.view.codex_page == 0); /* Wait for the first page. */
    sessions.detail_pages = 3;
    move(-800); assert(manager.view.codex_page == 1);
    move(-800); assert(manager.view.codex_page == 2);
    move(-800); assert(manager.view.codex_page == 2);
    move(800); assert(manager.view.codex_page == 1);
    open_selector(); assert(manager.view.codex_detail);
    release_chord(); update(20);
    assert(manager.view.codex_detail && manager.view.codex_page == 1);
    tap_sw(2);
    assert(!manager.view.codex_detail && manager.view.codex_row == 1 && manager.view.codex_id[0] == 3);
    tap_sw(1); assert(manager.view.codex_detail && manager.view.codex_page == 0);
    tap_sw(2);

    open_selector();
    assert(manager.view.codex_row == 1 && manager.view.selected == JINTAI_CODEX_APP);
    release_chord(); /* Selecting the original app resumes its state. */
    assert(manager.view.current == JINTAI_CODEX_APP && manager.view.codex_row == 1);
    update(20);
    open_selector(); move(-800);
    release_chord();
    assert(manager.view.current == JINTAI_CLAWD_APP && manager.view.clawd.pose == CLAWD_IDLE);
    update(20); tap_sw(0);
    clawd_pose_t initial_reaction = manager.view.clawd.pose;
    assert(action_pose(initial_reaction));

    /* Even a short chord is reserved and does not leak into the app. */
    press_chord(); assert(manager.view.choosing);
    release_chord();
    assert(!manager.view.choosing && manager.view.clawd.pose == initial_reaction);
    update(20);
    open_selector();
    input.joystick_y = -800; update(20); /* Keep stick deflected after release. */
    release_chord();
    assert(manager.view.current == 0 && manager.input_blocked);
    input.buttons[0] = true; update(20);
    assert(manager.view.clawd.pose == initial_reaction);
    input.buttons[0] = false; input.joystick_y = 0; update(20);
    assert(!manager.input_blocked);
    open_selector(); move(800); release_chord(); update(20);
    assert(manager.view.current == JINTAI_CLAWD_APP && manager.view.clawd.pose == initial_reaction);

    /* A late third key cannot turn already delivered events into a chord. */
    input.buttons[0] = true; update(20); update(181);
    input.buttons[1] = true; update(20);
    input.buttons[2] = true; update(20);
    assert(!manager.view.choosing && action_pose(manager.view.clawd.pose));
    release_chord();

    clawd_pose_t before_sleep = manager.clawd.recent_actions[0];
    tap_sw(3); assert(manager.view.clawd.pose == CLAWD_SLEEPING);
    update(60000);
    assert(manager.view.clawd.pose == CLAWD_SLEEPING && manager.clawd.recent_actions[0] == before_sleep);
    int x = manager.view.clawd.x, y = manager.view.clawd.y;
    input.joystick_x = -1000; input.joystick_y = 1000; update(20);
    assert(manager.view.clawd.x == x && manager.view.clawd.y == y);
    input.joystick_x = input.joystick_y = 0;
    tap_sw(3); assert(manager.view.clawd.pose == CLAWD_IDLE);
    input.joystick_x = -1000; input.joystick_y = 1000; update(20);
    assert(manager.view.clawd.pose == CLAWD_WALKING && manager.view.clawd.face_left);
    assert(manager.view.clawd.x < x && manager.view.clawd.y < y);
    input.joystick_x = input.joystick_y = 0; update(20);
    assert(manager.view.clawd.pose == CLAWD_IDLE);

    update(manager.clawd.next_auto_ms - now - 1);
    assert(manager.view.clawd.pose == CLAWD_IDLE);
    clawd_pose_t before_auto[2] = {manager.clawd.recent_actions[0], manager.clawd.recent_actions[1]};
    update(1);
    clawd_pose_t automatic = manager.view.clawd.pose;
    assert(action_pose(automatic) && automatic != before_auto[0] && automatic != before_auto[1]);
    update(7001); assert(manager.view.clawd.pose == CLAWD_IDLE);

    lab_touch_state_t touch = { .ready = true, .reported = 1, .valid = 1, .points = {{0, 0, 0}} };
    input.touch = &touch; input.touch_count = 1; update(20);
    assert(manager.view.clawd.pose == CLAWD_IDLE);
    touch.points[0].x = manager.view.clawd.x + 100;
    touch.points[0].y = manager.view.clawd.y - 100;
    update(20); assert(manager.view.clawd.pose == CLAWD_IDLE); /* Dragging in is not a tap. */
    touch.reported = touch.valid = input.touch_count = 0; update(20);
    touch.reported = touch.valid = input.touch_count = 1; update(20);
    clawd_pose_t touched = manager.view.clawd.pose;
    assert(action_pose(touched) && touched != automatic && touched != before_auto[0]);
    update(7001); assert(manager.view.clawd.pose == CLAWD_IDLE);
    touch.reported = touch.valid = input.touch_count = 0; update(20);

    unsigned seen = 0;
    for (unsigned i = 0; i < 64; ++i) {
        clawd_pose_t recent[2] = {manager.clawd.recent_actions[0], manager.clawd.recent_actions[1]};
        if (i % 4 == 3) {
            touch.reported = touch.valid = input.touch_count = 1; update(20);
            touch.reported = touch.valid = input.touch_count = 0; update(20);
        } else {
            tap_sw(i % 4);
        }
        clawd_pose_t pose = manager.view.clawd.pose;
        assert(action_pose(pose) && pose != recent[0] && pose != recent[1]);
        seen |= 1U << pose;
    }
    assert(seen == ((1U << CLAWD_POSE_COUNT) - 1) - (1U << CLAWD_IDLE) - (1U << CLAWD_WALKING) - (1U << CLAWD_SLEEPING));
    input.touch = NULL;

    clawd_pose_t last = manager.clawd.recent_actions[0], previous = manager.clawd.recent_actions[1];
    input.joystick_x = 800; update(manager.clawd.next_auto_ms - now);
    assert(manager.view.clawd.pose == CLAWD_WALKING && manager.clawd.recent_actions[0] == last);
    input.joystick_x = 0; update(20);
    assert(action_pose(manager.view.clawd.pose) && manager.view.clawd.pose != last && manager.view.clawd.pose != previous);

    open_selector(); move(800); release_chord(); update(20); update(20);
    assert(manager.view.current == JINTAI_CODEX_APP && manager.view.codex_id[0] == 2);
    sessions.count = 1;
    move(-800);
    move(800);
    assert(manager.view.codex_row == 0 && manager.view.codex_id[0] == 2);
    sessions.count = 0;
    update(20); assert(manager.view.codex_row == 0 && manager.view.codex_id[0] == 0);
    tap_sw(1); assert(!manager.view.codex_detail);

    app_manager_wake(&manager);
    press_chord(); update(1000);
    assert(!manager.view.choosing);
    release_chord();
    assert(!manager.input_blocked);
    open_selector();
    move(-800);
    assert(manager.view.selected != manager.view.current);
    app_manager_wake(&manager);
    assert(!manager.view.choosing && manager.view.selected == manager.view.current);
    release_chord();
    assert(manager.view.current == JINTAI_CODEX_APP);
    puts("app manager tests passed");
}
