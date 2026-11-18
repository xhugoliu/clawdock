#pragma once
#include <stdbool.h>
#include "mouse.h"

#define JINTAI_APP_COUNT 4
#define JINTAI_CODEX_APP 2
#define JINTAI_CLAWD_APP 3

typedef struct {
    mouse_t mouse;
} app_runtime_t;

const char *app_name(unsigned app);
const char *app_title(unsigned app);
void app_start(app_runtime_t *runtime, unsigned app);
void app_stop(app_runtime_t *runtime, unsigned app);
void app_button(app_runtime_t *runtime, unsigned app, unsigned button, bool pressed);
