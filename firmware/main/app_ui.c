#include "app_ui.h"
#include <stdio.h>
#include <stddef.h>

#define WIDTH 640
#define HEIGHT 480
#define PANEL_WIDTH 480
static uint16_t *fb; /* Used only by the display task. */

static uint16_t rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

/* One landscape pixel maps to the panel's clockwise quarter-turn. */
static void pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x < WIDTH && (unsigned)y < HEIGHT)
        fb[(size_t)x * PANEL_WIDTH + (PANEL_WIDTH - 1 - y)] = color;
}

static void rect(int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            pixel(xx, yy, color);
}

/* 5x7 glyphs used by the diagnostic screen and sleep warning. */
static const uint8_t font[][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}, {0,0,0,31,0,0,0},
    {0,0,31,2,4,8,31}, {0,0,0,0,0,6,6}
};

static void text_at(int x, int y, const char *s, int scale, uint16_t color)
{
    for (; *s; ++s, x += 6 * scale) {
        int index = *s >= '0' && *s <= '9' ? *s - '0' :
                    *s >= 'A' && *s <= 'Z' ? 10 + *s - 'A' :
                    *s == '-' ? 36 : *s == 'z' ? 37 : *s == '.' ? 38 : -1;
        if (index < 0) continue;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (font[index][row] & (1 << (4 - col)))
                    rect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

static void circle(int cx, int cy, int radius, uint16_t color)
{
    int outer = radius * radius;
    int inner = (radius - 4) * (radius - 4);
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            int distance = x * x + y * y;
            if (distance <= outer && distance >= inner) pixel(cx + x, cy + y, color);
        }
}

static void draw_diagnostics(const lab_display_state_t *state)
{
    const bool *buttons = state->buttons;
    const lab_touch_state_t *touch = &state->touch;
    bool adc_ok = state->adc_ok;
    const uint16_t bg = rgb(11, 19, 35), card = rgb(22, 35, 56);
    const uint16_t white = rgb(238, 245, 255), muted = rgb(137, 160, 186);
    const uint16_t accent = rgb(54, 222, 174), blue = rgb(77, 153, 255);
    rect(0, 0, WIDTH, HEIGHT, bg);
    rect(0, 0, WIDTH, 6, accent);
    text_at(26, 24, "JINTAI", 3, white);
    text_at(27, 55, "INPUT DIAGNOSTICS", 2, muted);
    text_at(27, 96, "BUTTONS", 2, muted);
    const char *labels[] = {"SW1", "SW2", "SW3", "JOY"};
    for (int i = 0; i < 4; ++i) {
        int bx = 26 + i * 150;
        rect(bx, 120, 134, 78, buttons[i] ? accent : card);
        text_at(bx + 12, 132, labels[i], 3, buttons[i] ? bg : white);
        text_at(bx + 12, 170, buttons[i] ? "PRESSED" : "READY", 2, buttons[i] ? bg : muted);
    }
    rect(26, 222, 588, 200, card);
    text_at(46, 242, "JOYSTICK", 2, white);
    char value[16];
    text_at(46, 284, "X", 3, blue);
    snprintf(value, sizeof(value), "%d", state->normalized_x);
    text_at(80, 284, adc_ok && state->joystick_calibrated ? value : "WAIT", 3, white);
    text_at(46, 338, "Y", 3, accent);
    snprintf(value, sizeof(value), "%d", state->normalized_y);
    text_at(80, 338, adc_ok && state->joystick_calibrated ? value : "WAIT", 3, white);
    /* Equal pixel scales around the calibrated center; values include the deadzone. */
    rect(360, 253, 212, 142, bg);
    circle(465, 324, 60, muted);
    rect(464, 265, 2, 118, muted);
    rect(406, 323, 118, 2, muted);
    if (adc_ok && state->joystick_calibrated) {
        int px = 465 + state->normalized_x * 55 / 1000;
        int py = 324 - state->normalized_y * 55 / 1000;
        rect(px - 5, py - 5, 10, 10, accent);
    }
    text_at(380, 401, state->joystick_calibrated ? "NORMALIZED" : "CENTER WAIT", 1, muted);
    text_at(27, 445, "XY NORMALIZED  DEADZONE 60", 2, muted);
    char touch_text[32];
    if (touch->ready) {
        snprintf(touch_text, sizeof(touch_text), "TOUCH %u MAX %u", touch->reported, touch->max_reported);
    } else {
        snprintf(touch_text, sizeof(touch_text), "TOUCH ERROR");
    }
    text_at(390, 26, touch_text, 2, white);
    snprintf(touch_text, sizeof(touch_text), "VALID %u", touch->valid);
    text_at(390, 52, touch_text, 2, muted);
    if (touch->reported > LAB_TOUCH_POINTS) text_at(500, 52, "OVER TWO", 1, white);
    const uint16_t touch_colors[2] = {rgb(54, 222, 174), rgb(255, 172, 76)};
    for (unsigned i = 0; i < touch->valid; ++i) {
        circle(touch->points[i].x, touch->points[i].y, 22, touch_colors[i]);
        rect((int)touch->points[i].x - 2, (int)touch->points[i].y - 2, 5, 5, touch_colors[i]);
    }
}

void app_ui_sleep_warning(uint16_t *buffer)
{
    fb = buffer;
    rect(0, 0, WIDTH, HEIGHT, rgb(0, 0, 0));
    text_at((WIDTH - 35 * 10) / 2, (HEIGHT - 7 * 10) / 2, "Zzz...", 10, rgb(255, 255, 255));
}

void app_ui_render(uint16_t *buffer, const lab_display_state_t *state)
{
    fb = buffer;
    const uint16_t bg = rgb(11, 19, 35), card = rgb(22, 35, 56);
    const uint16_t white = rgb(238, 245, 255), muted = rgb(137, 160, 186);
    const uint16_t accent = rgb(54, 222, 174);
    if (state->app.choosing) {
        rect(0, 0, WIDTH, HEIGHT, bg);
        rect(0, 0, WIDTH, 6, accent);
        text_at(26, 28, "SELECT APP", 3, white);
        text_at(26, 65, "HOLD SW KEYS  JOYSTICK UP DOWN", 2, muted);
        for (unsigned i = 0; i < JINTAI_APP_COUNT; ++i) {
            bool selected = i == state->app.selected;
            int y = 102 + (int)i * 73;
            rect(26, y, 588, 62, selected ? accent : card);
            text_at(46, y + 21, app_name(i), 3, selected ? bg : white);
            if (i == state->app.current) text_at(478, y + 23, "ACTIVE", 2, selected ? bg : muted);
        }
        text_at(26, 425, "RELEASE SW KEYS TO SWITCH", 2, muted);
    } else if (state->app.current == 0) {
        draw_diagnostics(state);
    } else {
        rect(0, 0, WIDTH, HEIGHT, bg);
        rect(0, 0, WIDTH, 6, accent);
        text_at(26, 28, app_name(state->app.current), 3, white);
        text_at(26, 72, "PLACEHOLDER", 2, muted);
        rect(26, 120, 588, 220, card);
        text_at(46, 150, "SW PRESS COUNT", 2, white);
        char count[20];
        snprintf(count, sizeof(count), "%u", state->app.presses);
        text_at(46, 190, count, 4, accent);
        text_at(46, 275, "STATE RESETS ON APP SWITCH", 2, muted);
        text_at(26, 425, "HOLD SW1 SW2 SW3 FOR APPS", 2, muted);
    }
}
