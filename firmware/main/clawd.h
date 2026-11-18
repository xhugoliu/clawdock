#pragma once
#include "touch_format.h"

typedef enum {
    CLAWD_IDLE,
    CLAWD_WALKING,
    CLAWD_HAPPY,
    CLAWD_SLEEPING,
    CLAWD_GROOVING,
    CLAWD_JUGGLING,
    CLAWD_EUREKA,
    CLAWD_HAT,
    CLAWD_COFFEE,
    CLAWD_DIZZY,
    CLAWD_SNACK,
    CLAWD_POSE_COUNT,
} clawd_pose_t;

typedef struct {
    int x, y;
    bool face_left;
    clawd_pose_t pose;
    int64_t pose_started_ms;
} clawd_view_t;

typedef struct {
    clawd_view_t view;
    int x_millipixels, y_millipixels;
    int64_t last_update_ms, action_ends_ms, next_auto_ms;
    clawd_pose_t recent_actions[2];
    uint32_t random_state;
    bool touch_down, action_requested;
} clawd_t;

void clawd_init(clawd_t *pet);
void clawd_button(clawd_t *pet, unsigned button, int64_t now_ms);
void clawd_update(clawd_t *pet, int stick_x, int stick_y, const lab_touch_state_t *touch, int64_t now_ms);
