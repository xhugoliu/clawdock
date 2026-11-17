#include "app_manager.h"
#include <string.h>

/* Timing starts from debounced switch transitions. */
#define CHORD_WINDOW_MS 180
#define NAV_THRESHOLD 600
#define NAV_RELEASE 300

void app_manager_init(app_manager_t *manager)
{
    memset(manager, 0, sizeof(*manager));
    app_start(&manager->runtime, manager->view.current);
}

static bool switches_released(const app_input_t *in)
{
    return !in->buttons[0] && !in->buttons[1] && !in->buttons[2];
}

static bool switches_pressed(const app_input_t *in)
{
    return in->buttons[0] && in->buttons[1] && in->buttons[2];
}

static void block_input(app_manager_t *m)
{
    m->input_blocked = true;
    m->navigation_armed = false;
    m->chord_pending = false;
    m->pending_count = 0;
    m->chord_locked = true;
}

void app_manager_wake(app_manager_t *manager)
{
    if (manager->view.current == 1) mouse_release(&manager->runtime.mouse);
    manager->mouse_chord = 0;
    block_input(manager);
}

static void deliver_button(app_manager_t *m, unsigned button, bool pressed)
{
    app_button(&m->runtime, m->view.current, button, pressed);
    m->view.presses = m->runtime.presses;
    m->view.mouse_scroll = m->runtime.mouse.scroll_mode;
}

static void flush_pending(app_manager_t *m)
{
    if (m->view.current == 1) {
        unsigned seen = 0, held = 0;
        for (unsigned i = 0; i < m->pending_count; ++i) {
            unsigned bit = 1U << m->pending[i].button;
            if (m->pending[i].pressed) { seen |= bit; held |= bit; }
            else held &= ~bit;
        }
        if (seen == 3 || seen == 6) {
            mouse_chord(&m->runtime.mouse, seen, true);
            if (held & seen) m->mouse_chord = seen;
            else mouse_chord(&m->runtime.mouse, seen, false);
            m->pending_count = 0;
            m->chord_pending = false;
            m->chord_locked = true;
            return;
        }
    }
    for (unsigned i = 0; i < m->pending_count; ++i)
        deliver_button(m, m->pending[i].button, m->pending[i].pressed);
    m->pending_count = 0;
    m->chord_pending = false;
}

static void select_current(app_manager_t *m)
{
    if (m->view.current != m->view.selected) {
        app_stop(&m->runtime, m->view.current);
        m->view.current = m->view.selected;
        app_start(&m->runtime, m->view.current);
        m->view.presses = m->runtime.presses;
        m->view.mouse_scroll = m->runtime.mouse.scroll_mode;
    }
    m->view.choosing = false;
    block_input(m);
}

static void navigate(app_manager_t *m, const app_input_t *in)
{
    if (!in->joystick_ready) return;
    int y = in->joystick_y;
    if (y > -NAV_RELEASE && y < NAV_RELEASE) m->navigation_armed = true;
    if (!m->navigation_armed) return;
    /* Diagnostic screen maps positive Y to up. The list wraps at both ends. */
    if (y >= NAV_THRESHOLD) {
        m->view.selected = (m->view.selected + JINTAI_APP_COUNT - 1) % JINTAI_APP_COUNT;
        m->navigation_armed = false;
    } else if (y <= -NAV_THRESHOLD) {
        m->view.selected = (m->view.selected + 1) % JINTAI_APP_COUNT;
        m->navigation_armed = false;
    }
}

void app_manager_update(app_manager_t *m, const app_input_t *in, int64_t now_ms)
{
    bool old[4];
    memcpy(old, m->previous, sizeof(old));
    memcpy(m->previous, in->buttons, sizeof(m->previous));

    if (m->input_blocked) {
        if (switches_released(in) && !in->buttons[3] && in->joystick_ready &&
            in->joystick_x > -NAV_RELEASE && in->joystick_x < NAV_RELEASE &&
            in->joystick_y > -NAV_RELEASE && in->joystick_y < NAV_RELEASE &&
            (!in->touch_available || (in->touch_ready && in->touch_count == 0))) {
            m->input_blocked = false;
            m->chord_locked = false;
            m->navigation_armed = true;
        }
        return;
    }

    if (m->view.choosing) {
        if (switches_released(in)) select_current(m);
        else navigate(m, in);
        return;
    }

    if (m->mouse_chord) {
        if (switches_released(in)) {
            mouse_chord(&m->runtime.mouse, m->mouse_chord, false);
            m->mouse_chord = 0;
            m->chord_locked = false;
        }
        return;
    }

    /* Once events were delivered, a late third switch cannot start a chord. */
    if (m->chord_pending && now_ms > m->chord_deadline_ms) {
        flush_pending(m);
        m->chord_locked = true;
        if (m->mouse_chord) {
            if (switches_released(in)) {
                mouse_chord(&m->runtime.mouse, m->mouse_chord, false);
                m->mouse_chord = 0;
                m->chord_locked = false;
            }
            return;
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (in->buttons[i] == old[i]) continue;
        if (!m->chord_locked && !m->chord_pending && in->buttons[i]) {
            m->chord_pending = true;
            m->chord_deadline_ms = now_ms + CHORD_WINDOW_MS;
        }
        if (m->chord_pending) {
            if (m->pending_count < sizeof(m->pending) / sizeof(m->pending[0])) {
                m->pending[m->pending_count].button = i;
                m->pending[m->pending_count++].pressed = in->buttons[i];
            } else {
                flush_pending(m);
                m->chord_locked = true;
                deliver_button(m, i, in->buttons[i]);
            }
        } else {
            deliver_button(m, i, in->buttons[i]);
        }
    }
    if (m->chord_pending && switches_pressed(in)) {
        m->chord_pending = false;
        m->pending_count = 0;
        if (m->view.current == 1) mouse_release(&m->runtime.mouse);
        m->view.choosing = true;
        m->view.selected = m->view.current;
        m->navigation_armed = true;
        navigate(m, in);
        return;
    }
    if (m->chord_pending && switches_released(in)) flush_pending(m);
    if (m->chord_locked && switches_released(in)) m->chord_locked = false;
}
