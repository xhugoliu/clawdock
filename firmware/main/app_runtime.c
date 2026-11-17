#include "app_runtime.h"
#include <string.h>

typedef struct {
    const char *name;
    void (*start)(app_runtime_t *runtime);
    void (*stop)(app_runtime_t *runtime);
    void (*button)(app_runtime_t *runtime, unsigned button, bool pressed);
} app_definition_t;

static void placeholder_button(app_runtime_t *runtime, unsigned button, bool pressed)
{
    if (button < 3 && pressed) ++runtime->presses;
}

/* Apps are built into the firmware. Only the active app has a runtime. */
static const app_definition_t apps[JINTAI_APP_COUNT] = {
    { .name = "INPUT LAB" },
    { .name = "APP ONE", .button = placeholder_button },
    { .name = "APP TWO", .button = placeholder_button },
    { .name = "APP THREE", .button = placeholder_button },
};

const char *app_name(unsigned app)
{
    return app < JINTAI_APP_COUNT ? apps[app].name : "UNKNOWN";
}

void app_start(app_runtime_t *runtime, unsigned app)
{
    memset(runtime, 0, sizeof(*runtime));
    if (app < JINTAI_APP_COUNT && apps[app].start) apps[app].start(runtime);
}

void app_stop(app_runtime_t *runtime, unsigned app)
{
    if (app < JINTAI_APP_COUNT && apps[app].stop) apps[app].stop(runtime);
    memset(runtime, 0, sizeof(*runtime));
}

void app_button(app_runtime_t *runtime, unsigned app, unsigned button, bool pressed)
{
    if (app < JINTAI_APP_COUNT && apps[app].button) apps[app].button(runtime, button, pressed);
}
