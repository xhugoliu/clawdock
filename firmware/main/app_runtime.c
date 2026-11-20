#include "app_runtime.h"
#include <string.h>

typedef struct {
    const char *name;
    const char *title;
    void (*start)(app_runtime_t *runtime);
    void (*stop)(app_runtime_t *runtime);
    void (*button)(app_runtime_t *runtime, unsigned button, bool pressed);
} app_definition_t;

static void mouse_app_button(app_runtime_t *runtime, unsigned button, bool pressed)
{
    mouse_button(&runtime->mouse, button, pressed);
}

/* Apps are built into the firmware. Only the active app has a runtime. */
static const app_definition_t apps[CLAWDOCK_APP_COUNT] = {
    [CLAWDOCK_CODEX_APP] = { .name = "SESSIONS", .title = "Sessions" },
    [CLAWDOCK_MOUSE_APP] = { .name = "MOUSE", .title = "Mouse", .button = mouse_app_button },
    [CLAWDOCK_INPUT_APP] = { .name = "INPUT LAB", .title = "Input Lab" },
    [CLAWDOCK_CLAWD_APP] = { .name = "CLAWD", .title = "Clawd" },
};

const char *app_name(unsigned app)
{
    return app < CLAWDOCK_APP_COUNT ? apps[app].name : "UNKNOWN";
}

const char *app_title(unsigned app)
{
    return app < CLAWDOCK_APP_COUNT ? apps[app].title : "Unknown";
}

void app_start(app_runtime_t *runtime, unsigned app)
{
    memset(runtime, 0, sizeof(*runtime));
    if (app < CLAWDOCK_APP_COUNT && apps[app].start) apps[app].start(runtime);
}

void app_stop(app_runtime_t *runtime, unsigned app)
{
    if (app < CLAWDOCK_APP_COUNT && apps[app].stop) apps[app].stop(runtime);
    memset(runtime, 0, sizeof(*runtime));
}

void app_button(app_runtime_t *runtime, unsigned app, unsigned button, bool pressed)
{
    if (app < CLAWDOCK_APP_COUNT && apps[app].button) apps[app].button(runtime, button, pressed);
}
