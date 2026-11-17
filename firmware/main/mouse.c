#include "mouse.h"

static void set_buttons(mouse_t *m, uint8_t buttons)
{
    if (buttons == m->buttons) return;
    m->buttons = buttons;
    if (m->count == MOUSE_QUEUE_SIZE) {
        /* Recover to a known released state before synchronizing the latest state. */
        m->head = m->count = 0;
        m->queue[m->count++] = 0;
    }
    m->queue[(m->head + m->count++) % MOUSE_QUEUE_SIZE] = buttons;
}

void mouse_button(mouse_t *m, unsigned button, bool pressed)
{
    if (button == 0) {
        if (pressed) {
            m->scroll_mode = !m->scroll_mode;
            m->pointer_accelerating = false;
            m->rem_x = m->rem_y = m->rem_wheel = m->rem_pan = 0;
        }
        return;
    }
    uint8_t bit = button == 1 ? MOUSE_RIGHT : button == 2 ? MOUSE_LEFT : 0;
    set_buttons(m, pressed ? m->buttons | bit : m->buttons & (uint8_t)~bit);
}

void mouse_chord(mouse_t *m, unsigned chord, bool pressed)
{
    uint8_t bit = chord == 3 ? MOUSE_BACK : chord == 6 ? MOUSE_FORWARD : 0;
    set_buttons(m, pressed ? m->buttons | bit : m->buttons & (uint8_t)~bit);
}

void mouse_release(mouse_t *m)
{
    set_buttons(m, 0);
    m->pointer_accelerating = false;
    m->rem_x = m->rem_y = m->rem_wheel = m->rem_pan = 0;
}

static int8_t axis(int value, int32_t *remainder, int scale)
{
    if (value > 1000) value = 1000;
    if (value < -1000) value = -1000;
    int magnitude = value < 0 ? -value : value;
    *remainder += (int32_t)((int64_t)value * magnitude * scale / 1000000);
    int result = *remainder / 1000;
    *remainder -= result * 1000;
    return (int8_t)result;
}

void mouse_motion(mouse_t *m, int x, int y, int64_t now_ms, int8_t *dx, int8_t *dy, int8_t *wheel, int8_t *pan)
{
    *dx = *dy = *wheel = *pan = 0;
    if (m->scroll_mode) {
        /* Scale 160/1000 gives up to eight wheel steps per second at 50 Hz. */
        *wheel = axis(y, &m->rem_wheel, 160);
        *pan = axis(-x, &m->rem_pan, 160);
    } else {
        /* Start at 22 pixels per 20 ms at full deflection, ramp to 40 in 1.2 s. */
        if (x > 100 || x < -100 || y > 100 || y < -100) {
            if (!m->pointer_accelerating) {
                m->pointer_accelerating = true;
                m->pointer_started_ms = now_ms;
            }
        } else {
            m->pointer_accelerating = false;
        }
        int64_t elapsed = m->pointer_accelerating ? now_ms - m->pointer_started_ms : 0;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > 1200) elapsed = 1200;
        int scale = 22000 + (int)(18000 * elapsed / 1200);
        *dx = axis(x, &m->rem_x, scale);
        *dy = axis(-y, &m->rem_y, scale);
        /* Limit the combined diagonal speed, not just each axis. */
        int squared = *dx * *dx + *dy * *dy;
        if (squared > 40 * 40) {
            int magnitude = 40;
            while (magnitude * magnitude < squared) ++magnitude;
            *dx = (int8_t)(*dx * 40 / magnitude);
            *dy = (int8_t)(*dy * 40 / magnitude);
        }
    }
}

bool mouse_peek(const mouse_t *m, uint8_t *buttons)
{
    if (!m->count) return false;
    *buttons = m->queue[m->head];
    return true;
}
void mouse_pop(mouse_t *m)
{
    if (m->count) { m->head = (m->head + 1) % MOUSE_QUEUE_SIZE; --m->count; }
}
