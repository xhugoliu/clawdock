#pragma once
#include <stdbool.h>
#include "mouse.h"

enum {
    CLAWDOCK_CODEX_APP,
    CLAWDOCK_MOUSE_APP,
    CLAWDOCK_INPUT_APP,
    CLAWDOCK_CLAWD_APP,
    CLAWDOCK_APP_COUNT,
};

typedef struct {
    mouse_t mouse;
} app_runtime_t;

const char *app_name(unsigned app);
const char *app_title(unsigned app);
void app_start(app_runtime_t *runtime, unsigned app);
void app_stop(app_runtime_t *runtime, unsigned app);
void app_button(app_runtime_t *runtime, unsigned app, unsigned button, bool pressed);
