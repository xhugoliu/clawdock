#include "clawd.h"
#include "app_ui_theme.h"

enum {
    PIXEL_UNIT = 1000,
    WALK_SPEED = 175,
    STICK_THRESHOLD = 140,
    AUTO_ACTION_MS = 30000,
    PET_LEFT = UI_LEFT_MARGIN + 180,
    PET_RIGHT = UI_WIDTH - 180,
    PET_TOP = UI_CONTENT_TOP + 200,
    PET_BOTTOM = UI_HEIGHT - 82,
};

static const unsigned action_duration_ms[CLAWD_POSE_COUNT] = {
    [CLAWD_HAPPY] = 1000,
    [CLAWD_GROOVING] = 3200,
    [CLAWD_JUGGLING] = 3600,
    [CLAWD_EUREKA] = 6000,
    [CLAWD_HAT] = 7000,
    [CLAWD_COFFEE] = 4000,
    [CLAWD_DIZZY] = 4000,
    [CLAWD_SNACK] = 2000,
};

unsigned clawd_action_duration(clawd_pose_t pose)
{
    return action_duration_ms[pose];
}

static void set_pose(clawd_t *pet, clawd_pose_t pose, int64_t now_ms)
{
    pet->view.pose = pose;
    pet->view.pose_started_ms = now_ms;
    unsigned duration = clawd_action_duration(pose);
    pet->action_ends_ms = duration ? now_ms + duration : 0;
}

void clawd_init(clawd_t *pet)
{
    *pet = (clawd_t){ .view = {
        .x = (UI_WIDTH + UI_LEFT_MARGIN) / 2,
        .y = (PET_TOP + PET_BOTTOM) / 2 + 24,
    } };
    pet->x_millipixels = pet->view.x * PIXEL_UNIT;
    pet->y_millipixels = pet->view.y * PIXEL_UNIT;
}

uint32_t clawd_next_random(clawd_action_picker_t *picker, int64_t now_ms)
{
    uint32_t random = picker->random_state ^ (uint32_t)now_ms;
    if (!random) random = 0x9e3779b9;
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    picker->random_state = random;
    return random;
}

clawd_pose_t clawd_choose_action(clawd_action_picker_t *picker, unsigned preferred, int64_t now_ms)
{
    unsigned available = CLAWD_FUN_ACTIONS & ~((1U << picker->recent[0]) | (1U << picker->recent[1]));
    unsigned eligible = available & preferred;
    if (!eligible) eligible = available;
    clawd_pose_t choices[CLAWD_POSE_COUNT];
    unsigned count = 0;
    for (clawd_pose_t pose = CLAWD_IDLE; pose < CLAWD_POSE_COUNT; ++pose)
        if (eligible & (1U << pose)) choices[count++] = pose;

    picker->recent[1] = picker->recent[0];
    picker->recent[0] = choices[clawd_next_random(picker, now_ms) % count];
    return picker->recent[0];
}

void clawd_button(clawd_t *pet, unsigned button, int64_t now_ms)
{
    if (button < 3) pet->action_requested = true;
    if (button == 3) {
        bool waking = pet->view.pose == CLAWD_SLEEPING;
        pet->action_requested = false;
        set_pose(pet, waking ? CLAWD_IDLE : CLAWD_SLEEPING, now_ms);
        pet->next_auto_ms = now_ms + AUTO_ACTION_MS;
    }
}

static void update_touch(clawd_t *pet, const lab_touch_state_t *touch)
{
    if (!touch || !touch->ready) return;
    if (!pet->touch_down) {
        for (unsigned i = 0; i < touch->valid; ++i) {
            int x = touch->points[i].x - pet->view.x;
            int y = touch->points[i].y - pet->view.y;
            if (x >= -172 && x < 172 && y >= -208 && y < 24) {
                pet->action_requested = true;
                break;
            }
        }
    }
    pet->touch_down = touch->reported > 0;
}

static int move_axis(int position, int stick, int elapsed, int minimum, int maximum)
{
    int next = position + stick * elapsed * WALK_SPEED / 1000;
    if (next < minimum * PIXEL_UNIT) return minimum * PIXEL_UNIT;
    if (next > maximum * PIXEL_UNIT) return maximum * PIXEL_UNIT;
    return next;
}

void clawd_update(clawd_t *pet, int stick_x, int stick_y, const lab_touch_state_t *touch, int64_t now_ms)
{
    int64_t elapsed = now_ms - pet->last_update_ms;
    pet->last_update_ms = now_ms;
    /* Time spent out of the app never becomes a large movement on re-entry. */
    int step = elapsed > 0 && elapsed <= 60 ? elapsed : 0;
    if (!pet->view.pose_started_ms) pet->view.pose_started_ms = now_ms;
    if (!pet->next_auto_ms) pet->next_auto_ms = now_ms + AUTO_ACTION_MS;
    update_touch(pet, touch);

    bool auto_due = now_ms >= pet->next_auto_ms;
    bool sleeping = pet->view.pose == CLAWD_SLEEPING;
    if (sleeping && auto_due) pet->next_auto_ms = now_ms + AUTO_ACTION_MS;

    if (stick_x > -STICK_THRESHOLD && stick_x < STICK_THRESHOLD) stick_x = 0;
    if (stick_y > -STICK_THRESHOLD && stick_y < STICK_THRESHOLD) stick_y = 0;
    if (!stick_x && !stick_y) {
        if (pet->view.pose == CLAWD_WALKING || (pet->action_ends_ms && now_ms >= pet->action_ends_ms))
            set_pose(pet, CLAWD_IDLE, now_ms);
        if (pet->action_requested || (auto_due && pet->view.pose == CLAWD_IDLE)) {
            pet->action_requested = false;
            if (auto_due || sleeping) pet->next_auto_ms = now_ms + AUTO_ACTION_MS;
            set_pose(pet, clawd_choose_action(&pet->actions, CLAWD_FUN_ACTIONS, now_ms), now_ms);
        }
        return;
    }
    if (sleeping) return;
    if (pet->view.pose != CLAWD_WALKING) set_pose(pet, CLAWD_WALKING, now_ms);
    if (stick_x) pet->view.face_left = stick_x < 0;

    pet->x_millipixels = move_axis(pet->x_millipixels, stick_x, step, PET_LEFT, PET_RIGHT);
    pet->y_millipixels = move_axis(pet->y_millipixels, -stick_y, step, PET_TOP, PET_BOTTOM);
    pet->view.x = pet->x_millipixels / PIXEL_UNIT;
    pet->view.y = pet->y_millipixels / PIXEL_UNIT;
}
