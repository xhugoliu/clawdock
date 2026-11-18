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
    clawd_init(&manager->clawd);
    manager->view.clawd = manager->clawd.view;
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
    m->clawd.touch_down = false;
    m->clawd.action_requested = false;
}

void app_manager_wake(app_manager_t *manager)
{
    if (manager->view.current == 1) mouse_release(&manager->runtime.mouse);
    manager->view.choosing = false;
    manager->view.selected = manager->view.current;
    manager->mouse_chord = 0;
    block_input(manager);
}

static void deliver_button(app_manager_t *m, unsigned button, bool pressed, int64_t now_ms)
{
    if (m->view.current == JINTAI_CODEX_APP && pressed) {
        if (button == 1 && m->view.codex_has_selection && !m->view.codex_detail) {
            m->view.codex_detail = true;
            m->view.codex_page = 0;
        } else if (button == 2) {
            m->view.codex_detail = false;
        }
    }
    if (m->view.current == JINTAI_CLAWD_APP && pressed) clawd_button(&m->clawd, button, now_ms);
    app_button(&m->runtime, m->view.current, button, pressed);
    m->view.mouse_scroll = m->runtime.mouse.scroll_mode;
}

static void flush_pending(app_manager_t *m, int64_t now_ms)
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
        deliver_button(m, m->pending[i].button, m->pending[i].pressed, now_ms);
    m->pending_count = 0;
    m->chord_pending = false;
}

static void select_current(app_manager_t *m)
{
    if (m->view.current != m->view.selected) {
        app_stop(&m->runtime, m->view.current);
        m->view.current = m->view.selected;
        app_start(&m->runtime, m->view.current);
        if (m->view.current == JINTAI_CLAWD_APP) m->clawd.next_auto_ms = 0;
        m->view.mouse_scroll = m->runtime.mouse.scroll_mode;
        m->view.codex_row = 0;
        memset(m->view.codex_id, 0, sizeof(m->view.codex_id));
        m->view.codex_has_selection = false;
        m->view.codex_detail = false;
        m->view.codex_page = 0;
    }
    m->view.choosing = false;
    block_input(m);
}

static int navigation_step(app_manager_t *m, const app_input_t *in)
{
    if (!in->joystick_ready) return 0;
    int y = in->joystick_y;
    if (y > -NAV_RELEASE && y < NAV_RELEASE) m->navigation_armed = true;
    if (!m->navigation_armed || (y > -NAV_THRESHOLD && y < NAV_THRESHOLD)) return 0;
    m->navigation_armed = false;
    return y > 0 ? -1 : 1;
}

static void navigate(app_manager_t *m, const app_input_t *in)
{
    int step = navigation_step(m, in);
    m->view.selected = (m->view.selected + JINTAI_APP_COUNT + step) % JINTAI_APP_COUNT;
}

static void navigate_codex(app_manager_t *m, const app_input_t *in)
{
    const codex_summary_t *list = in->codex;
    int step = navigation_step(m, in);
    if (m->view.codex_detail) {
        if (!list || !list->detail_pages) return;
        unsigned page = m->view.codex_page < list->detail_pages ? m->view.codex_page : list->detail_pages - 1;
        if (step < 0 && page > 0) --page;
        if (step > 0 && page + 1 < list->detail_pages) ++page;
        m->view.codex_page = page;
        return;
    }
    if (!list || !list->count) {
        m->view.codex_row = 0;
        memset(m->view.codex_id, 0, sizeof(m->view.codex_id));
        m->view.codex_has_selection = false;
        return;
    }
    unsigned row = m->view.codex_row < list->count ? m->view.codex_row : list->count - 1;
    for (unsigned i = 0; i < list->count; ++i) {
        if (memcmp(m->view.codex_id, list->ids[i], CODEX_ID_BYTES) != 0) continue;
        row = i;
        break;
    }
    row = (row + list->count + step) % list->count;
    m->view.codex_row = row;
    memcpy(m->view.codex_id, list->ids[row], CODEX_ID_BYTES);
    m->view.codex_has_selection = true;
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
        flush_pending(m, now_ms);
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
                flush_pending(m, now_ms);
                m->chord_locked = true;
                deliver_button(m, i, in->buttons[i], now_ms);
            }
        } else {
            deliver_button(m, i, in->buttons[i], now_ms);
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
    if (m->chord_pending && switches_released(in)) flush_pending(m, now_ms);
    if (m->chord_locked && switches_released(in)) m->chord_locked = false;
    if (m->view.current == JINTAI_CODEX_APP && !m->chord_pending) navigate_codex(m, in);
    if (m->view.current == JINTAI_CLAWD_APP) {
        bool active = !m->chord_pending;
        if (active && in->buttons[3] && !old[3]) clawd_button(&m->clawd, 3, now_ms);
        clawd_update(&m->clawd, active && in->joystick_ready ? in->joystick_x : 0,
                     active && in->joystick_ready ? in->joystick_y : 0, active ? in->touch : NULL, now_ms);
        m->view.clawd = m->clawd.view;
    }
}
