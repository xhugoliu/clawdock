#pragma once

#include "clawd.h"
#include "codex_monitor.h"

typedef enum {
    SESSIONS_CLAWD_NONE,
    SESSIONS_CLAWD_STARTED,
    SESSIONS_CLAWD_CANCELLED,
    SESSIONS_CLAWD_COMPLETED,
    SESSIONS_CLAWD_FAILED,
} sessions_clawd_event_t;

typedef struct {
    uint8_t id[CODEX_ID_BYTES];
    uint8_t state;
    int64_t seen_ms;
} sessions_clawd_session_t;

typedef struct {
    clawd_view_t view;
    clawd_action_picker_t actions;
    sessions_clawd_session_t sessions[2 * CODEX_MAX_ROWS];
    int position_millipixels;
    int64_t last_update_ms, last_snapshot_ms, action_ends_ms, patrol_ends_ms;
    sessions_clawd_event_t pending;
    bool running, pet_pending;
} sessions_clawd_t;

void sessions_clawd_pause(sessions_clawd_t *clawd);
void sessions_clawd_update(sessions_clawd_t *clawd, const codex_snapshot_t *snapshot,
                           bool pet_requested, int stick_x, int64_t now_ms);
