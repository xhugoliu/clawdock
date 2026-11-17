#pragma once
#include <stdbool.h>

#define JINTAI_APP_COUNT 4

typedef struct {
    unsigned presses;
} app_runtime_t;

const char *app_name(unsigned app);
void app_start(app_runtime_t *runtime, unsigned app);
void app_stop(app_runtime_t *runtime, unsigned app);
void app_button(app_runtime_t *runtime, unsigned app, unsigned button, bool pressed);
