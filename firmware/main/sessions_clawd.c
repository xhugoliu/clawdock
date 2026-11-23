#include "sessions_clawd.h"
#include "app_ui_theme.h"
#include <string.h>

enum {
    PIXEL_UNIT = 1000,
    WALK_SPEED = 42,
    MANUAL_WALK_SPEED = 150,
    STICK_THRESHOLD = 140,
    WALK_HALF_WIDTH = 60,
    LEFT_END = (UI_LEFT_MARGIN + WALK_HALF_WIDTH) * PIXEL_UNIT,
    RIGHT_END = (UI_WIDTH - WALK_HALF_WIDTH) * PIXEL_UNIT,
    SLEEP_CENTER = UI_WIDTH / 2 * PIXEL_UNIT,
};

static const unsigned preferred_actions[] = {
    [SESSIONS_CLAWD_STARTED] = (1U << CLAWD_EUREKA) | (1U << CLAWD_GROOVING) | (1U << CLAWD_JUGGLING),
    [SESSIONS_CLAWD_CANCELLED] = (1U << CLAWD_HAT) | (1U << CLAWD_GROOVING) | (1U << CLAWD_SNACK),
    [SESSIONS_CLAWD_COMPLETED] = (1U << CLAWD_HAPPY) | (1U << CLAWD_COFFEE) | (1U << CLAWD_SNACK),
    [SESSIONS_CLAWD_FAILED] = (1U << CLAWD_DIZZY) | (1U << CLAWD_HAT) | (1U << CLAWD_JUGGLING),
};

static void set_pose(sessions_clawd_t *clawd, clawd_pose_t pose, int64_t now_ms)
{
    clawd->view.pose = pose;
    clawd->view.pose_started_ms = now_ms;
}

static void set_patrol_pose(sessions_clawd_t *clawd, clawd_pose_t pose, int64_t now_ms)
{
    uint32_t random = clawd_next_random(&clawd->actions, now_ms);
    bool walking = pose == CLAWD_WALKING;
    if (walking && clawd->view.pose == CLAWD_IDLE && clawd->patrol_ends_ms && !(random >> 16 & 3U))
        clawd->view.face_left = !clawd->view.face_left;
    unsigned duration = walking ? 1800 + random % 1801 : 2000 + random % 1201;
    clawd->patrol_ends_ms = now_ms + duration;
    set_pose(clawd, pose, now_ms);
}

void sessions_clawd_pause(sessions_clawd_t *clawd)
{
    if (!clawd->last_update_ms) return;
    memset(clawd->sessions, 0, sizeof(clawd->sessions));
    clawd->last_snapshot_ms = 0;
    clawd->last_update_ms = 0;
    clawd->action_ends_ms = 0;
    clawd->patrol_ends_ms = 0;
    clawd->pending = SESSIONS_CLAWD_NONE;
    clawd->running = false;
    clawd->pet_pending = false;
}

static sessions_clawd_event_t session_event(uint8_t previous, uint8_t current)
{
    if (previous == current) return SESSIONS_CLAWD_NONE;
    if (current == CODEX_RUNNING) return SESSIONS_CLAWD_STARTED;
    if (previous != CODEX_RUNNING) return SESSIONS_CLAWD_NONE;
    switch (current) {
        case CODEX_COMPLETED: return SESSIONS_CLAWD_COMPLETED;
        case CODEX_FAILED: return SESSIONS_CLAWD_FAILED;
        case CODEX_CANCELLED: return SESSIONS_CLAWD_CANCELLED;
        default: return SESSIONS_CLAWD_NONE;
    }
}

static void read_sessions(sessions_clawd_t *clawd, const codex_summary_t *sessions)
{
    bool seed = clawd->last_snapshot_ms == 0;
    clawd->last_snapshot_ms = sessions->received_ms;
    clawd->running = false;
    for (unsigned row = 0; row < sessions->count; ++row) {
        uint8_t state = sessions->states[row];
        if (state == CODEX_RUNNING) clawd->running = true;
        sessions_clawd_session_t *known = NULL, *oldest = &clawd->sessions[0];
        for (unsigned i = 0; i < sizeof(clawd->sessions) / sizeof(clawd->sessions[0]); ++i) {
            sessions_clawd_session_t *entry = &clawd->sessions[i];
            if (entry->seen_ms && memcmp(entry->id, sessions->ids[row], CODEX_ID_BYTES) == 0) {
                known = entry;
                break;
            }
            if (entry->seen_ms < oldest->seen_ms) oldest = entry;
        }
        if (!seed) {
            sessions_clawd_event_t event = session_event(known ? known->state : 0, state);
            if (event > clawd->pending) clawd->pending = event;
        }
        if (!known) known = oldest;
        memcpy(known->id, sessions->ids[row], CODEX_ID_BYTES);
        known->state = state;
        known->seen_ms = sessions->received_ms;
    }
}

static bool walk(sessions_clawd_t *clawd, int speed, int target, int elapsed, int64_t now_ms)
{
    int position = clawd->position_millipixels;
    int next = position + speed * elapsed;
    if (speed < 0 ? next < target : next > target) next = target;
    if (clawd->view.pose != CLAWD_WALKING) set_pose(clawd, CLAWD_WALKING, now_ms);
    clawd->view.face_left = speed < 0;
    clawd->position_millipixels = next;
    clawd->view.x = next / PIXEL_UNIT;
    return next != position && next == target;
}

void sessions_clawd_update(sessions_clawd_t *clawd, const codex_summary_t *sessions,
                           bool pet_requested, int stick_x, int64_t now_ms)
{
    bool sessions_current = codex_summary_is_current(sessions, now_ms);
    if (sessions_current && clawd->last_snapshot_ms != sessions->received_ms)
        read_sessions(clawd, sessions);

    int64_t elapsed = clawd->last_update_ms ? now_ms - clawd->last_update_ms : 0;
    clawd->last_update_ms = now_ms;
    if (elapsed < 0 || elapsed > 150) elapsed = 0;
    if (!clawd->position_millipixels) {
        clawd->position_millipixels = LEFT_END;
        clawd->view.x = LEFT_END / PIXEL_UNIT;
    }

    if (pet_requested) clawd->pet_pending = true;
    if (stick_x <= -STICK_THRESHOLD || stick_x >= STICK_THRESHOLD) {
        clawd->action_ends_ms = 0;
        clawd->patrol_ends_ms = 0;
        int speed = stick_x * MANUAL_WALK_SPEED / 1000;
        walk(clawd, speed, speed < 0 ? LEFT_END : RIGHT_END, elapsed, now_ms);
        return;
    }

    if (!clawd->pet_pending && now_ms < clawd->action_ends_ms) return;
    if (clawd->pet_pending || clawd->pending != SESSIONS_CLAWD_NONE) {
        unsigned actions = clawd->pet_pending ? CLAWD_FUN_ACTIONS : preferred_actions[clawd->pending];
        clawd_pose_t pose = clawd_choose_action(&clawd->actions, actions, now_ms);
        set_pose(clawd, pose, now_ms);
        clawd->action_ends_ms = now_ms + clawd_action_duration(pose);
        clawd->patrol_ends_ms = 0;
        if (!clawd->pet_pending) clawd->pending = SESSIONS_CLAWD_NONE;
        clawd->pet_pending = false;
        return;
    }

    int position = clawd->position_millipixels;
    if (!sessions_current || !clawd->running) {
        clawd->patrol_ends_ms = 0;
        if (position != SLEEP_CENTER) {
            int speed = position < SLEEP_CENTER ? WALK_SPEED : -WALK_SPEED;
            if (!walk(clawd, speed, SLEEP_CENTER, elapsed, now_ms)) return;
        }
        if (clawd->view.pose != CLAWD_SLEEPING) set_pose(clawd, CLAWD_SLEEPING, now_ms);
        return;
    }

    if (!clawd->patrol_ends_ms) set_patrol_pose(clawd, CLAWD_WALKING, now_ms);
    else if (now_ms >= clawd->patrol_ends_ms)
        set_patrol_pose(clawd, clawd->view.pose == CLAWD_WALKING ? CLAWD_IDLE : CLAWD_WALKING, now_ms);
    if (clawd->view.pose == CLAWD_IDLE) return;

    bool leftward = position >= RIGHT_END || (position > LEFT_END && clawd->view.face_left);
    bool at_end = walk(clawd, leftward ? -WALK_SPEED : WALK_SPEED, leftward ? LEFT_END : RIGHT_END, elapsed, now_ms);
    if (at_end) set_patrol_pose(clawd, CLAWD_IDLE, now_ms);
}
