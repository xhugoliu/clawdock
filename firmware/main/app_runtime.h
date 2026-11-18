#pragma once
#include <stdbool.h>
#include "mouse.h"

enum {
    JINTAI_CODEX_APP,
    JINTAI_MOUSE_APP,
    JINTAI_INPUT_APP,
    JINTAI_CLAWD_APP,
    JINTAI_APP_COUNT,
};

typedef struct {
    mouse_t mouse;
} app_runtime_t;

const char *app_name(unsigned app);
const char *app_title(unsigned app);
void app_start(app_runtime_t *runtime, unsigned app);
void app_stop(app_runtime_t *runtime, unsigned app);
void app_button(app_runtime_t *runtime, unsigned app, unsigned button, bool pressed);
