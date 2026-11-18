#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "app_runtime.h"
#include "clawd.h"
#include "codex_monitor.h"

typedef struct {
    bool buttons[4];
    bool joystick_ready;
    int joystick_x;
    int joystick_y;
    bool touch_available;
    bool touch_ready;
    unsigned touch_count;
    const lab_touch_state_t *touch;
    const codex_summary_t *codex;
} app_input_t;

typedef struct {
    unsigned current;
    unsigned selected;
    bool choosing;
    bool mouse_scroll;
    clawd_view_t clawd;
    unsigned codex_row;
    uint8_t codex_id[CODEX_ID_BYTES];
    bool codex_has_selection;
    bool codex_detail;
    unsigned codex_page;
    unsigned sessions_clawd_requests;
    int sessions_clawd_stick_x;
} app_view_t;

typedef struct {
    app_view_t view;
    app_runtime_t runtime;
    clawd_t clawd;
    bool previous[4];
    bool input_blocked;
    bool navigation_armed;
    bool chord_pending;
    bool chord_locked;
    unsigned mouse_chord;
    int64_t chord_deadline_ms;
    struct { unsigned button; bool pressed; } pending[6];
    unsigned pending_count;
} app_manager_t;

void app_manager_init(app_manager_t *manager);
void app_manager_wake(app_manager_t *manager);
void app_manager_update(app_manager_t *manager, const app_input_t *input, int64_t now_ms);
