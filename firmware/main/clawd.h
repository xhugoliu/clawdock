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

enum {
    CLAWD_FUN_ACTIONS = ((1U << CLAWD_POSE_COUNT) - 1) &
                        ~((1U << CLAWD_IDLE) | (1U << CLAWD_WALKING) | (1U << CLAWD_SLEEPING)),
};

typedef struct {
    clawd_pose_t recent[2];
    uint32_t random_state;
} clawd_action_picker_t;

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
    clawd_action_picker_t actions;
    bool touch_down, action_requested;
} clawd_t;

uint32_t clawd_next_random(clawd_action_picker_t *picker, int64_t now_ms);
clawd_pose_t clawd_choose_action(clawd_action_picker_t *picker, unsigned preferred, int64_t now_ms);
unsigned clawd_action_duration(clawd_pose_t pose);
void clawd_init(clawd_t *pet);
void clawd_button(clawd_t *pet, unsigned button, int64_t now_ms);
void clawd_update(clawd_t *pet, int stick_x, int stick_y, const lab_touch_state_t *touch, int64_t now_ms);
