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

static void set_pose(clawd_t *pet, clawd_pose_t pose, int64_t now_ms)
{
    pet->view.pose = pose;
    pet->view.pose_started_ms = now_ms;
    unsigned duration = action_duration_ms[pose];
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

static void react(clawd_t *pet, int64_t now_ms)
{
    static const clawd_pose_t actions[] = {CLAWD_HAPPY, CLAWD_GROOVING, CLAWD_JUGGLING, CLAWD_EUREKA,
                                          CLAWD_HAT, CLAWD_COFFEE, CLAWD_DIZZY, CLAWD_SNACK};
    clawd_pose_t choices[sizeof(actions) / sizeof(actions[0])];
    unsigned count = 0;
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        if (actions[i] != pet->recent_actions[0] && actions[i] != pet->recent_actions[1])
            choices[count++] = actions[i];
    }

    uint32_t random = pet->random_state ^ (uint32_t)now_ms;
    if (!random) random = 0x9e3779b9;
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    pet->random_state = random;
    clawd_pose_t pose = choices[random % count];
    pet->recent_actions[1] = pet->recent_actions[0];
    pet->recent_actions[0] = pose;
    set_pose(pet, pose, now_ms);
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
            react(pet, now_ms);
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
