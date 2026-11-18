#include "app_ui.h"
#include "app_ui_theme.h"
#include <stdio.h>
#include <string.h>

#define WIDTH 640
#define HEIGHT 480
#define CONTENT_WIDTH (WIDTH - UI_LEFT_MARGIN)
#define CONTENT_X (UI_LEFT_MARGIN + UI_INSET)
#define FOOTER_BASELINE (HEIGHT - UI_FOOTER_DESCENT)
#define CONTENT_BOTTOM (FOOTER_BASELINE - 30)

static uint16_t *fb; /* Used only by the display task. */
static const char apps_hint[] = "Apps: hold all 3 keys";
static const char move_hint[] = "Stick: up / down";

extern const uint8_t ui_font_data[] __asm__("_binary_app_ui_font_bin_start");

/* One landscape pixel maps to the panel's clockwise quarter-turn. */
static void pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x < WIDTH && (unsigned)y < HEIGHT)
        fb[(size_t)x * HEIGHT + (HEIGHT - 1 - y)] = color;
}

static void rect(int x, int y, int w, int h, uint16_t color)
{
    int left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    int right = x + w > WIDTH ? WIDTH : x + w;
    int bottom = y + h > HEIGHT ? HEIGHT : y + h;
    if (left >= right || top >= bottom) return;
    for (int col = left; col < right; ++col) {
        uint16_t *start = fb + (size_t)col * HEIGHT + HEIGHT - bottom;
        uint16_t *end = start + bottom - top;
        while (start < end) *start++ = color;
    }
}

static void begin_frame(uint16_t *buffer)
{
    fb = buffer;
    rect(0, 0, WIDTH, HEIGHT, UI_CANVAS);
}

static uint16_t blend(uint16_t under, uint16_t ink, unsigned alpha)
{
    unsigned back = 15 - alpha;
    unsigned r = ((under >> 11) * back + (ink >> 11) * alpha + 7) / 15;
    unsigned g = (((under >> 5) & 63) * back + ((ink >> 5) & 63) * alpha + 7) / 15;
    unsigned b = ((under & 31) * back + (ink & 31) * alpha + 7) / 15;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static const uint8_t *font_face(ui_font_t font)
{
    const uint8_t *entry = ui_font_data + (unsigned)font * 4;
    uint32_t offset = (uint32_t)entry[0] | (uint32_t)entry[1] << 8 |
                      (uint32_t)entry[2] << 16 | (uint32_t)entry[3] << 24;
    return ui_font_data + offset;
}

static const uint8_t *font_glyph(const uint8_t *face, unsigned char character)
{
    if (character < ' ' || character > '~') character = '?';
    /* Advance, two bearings, width, height and a little-endian mask offset. */
    return face + (character - ' ') * 7;
}

static int text_width(const char *text, ui_font_t font)
{
    const uint8_t *face = font_face(font);
    int width = 0;
    for (; *text; ++text) width += font_glyph(face, *text)[0];
    return width;
}

static void text_at(int x, int baseline, const char *text, ui_font_t font, uint16_t ink)
{
    const uint8_t *face = font_face(font);
    for (; *text; ++text) {
        const uint8_t *glyph = font_glyph(face, *text);
        int left = x + (int8_t)glyph[1], top = baseline + (int8_t)glyph[2];
        unsigned width = glyph[3], height = glyph[4];
        const uint8_t *mask = face + (glyph[5] | (unsigned)glyph[6] << 8);
        for (unsigned row = 0; row < height; ++row) {
            int py = top + (int)row;
            if ((unsigned)py >= HEIGHT) continue;
            for (unsigned col = 0; col < width; ++col) {
                unsigned index = row * width + col;
                unsigned alpha = index & 1 ? mask[index / 2] & 15 : mask[index / 2] >> 4;
                int px = left + (int)col;
                if (alpha && (unsigned)px < WIDTH) {
                    uint16_t *target = fb + (size_t)px * HEIGHT + HEIGHT - 1 - py;
                    *target = alpha == 15 ? ink : blend(*target, ink, alpha);
                }
            }
        }
        x += glyph[0];
    }
}

static void text_end(int right, int baseline, const char *text, ui_font_t font, uint16_t ink)
{
    text_at(right - text_width(text, font), baseline, text, font, ink);
}

static void page_title(const char *title)
{
    text_at(UI_LEFT_MARGIN, UI_TITLE_BASELINE, title, UI_FONT_TITLE, UI_TEXT);
}

static void footer(const char *left, const char *center, const char *right)
{
    if (left) text_at(UI_LEFT_MARGIN, FOOTER_BASELINE, left, UI_FONT_HINT, UI_SECONDARY);
    if (center) text_at(UI_LEFT_MARGIN + (CONTENT_WIDTH - text_width(center, UI_FONT_HINT)) / 2, FOOTER_BASELINE,
                        center, UI_FONT_HINT, UI_SECONDARY);
    if (right) text_end(WIDTH, FOOTER_BASELINE, right, UI_FONT_HINT, UI_SECONDARY);
}

static void list_row(int y, int height, bool focused)
{
    enum { MARK_WIDTH = 3, MARK_HEIGHT = 24 };
    rect(UI_LEFT_MARGIN, y, CONTENT_WIDTH, height, focused ? UI_ENGAGED : UI_SURFACE);
    if (focused) rect(UI_LEFT_MARGIN, y + (height - MARK_HEIGHT) / 2, MARK_WIDTH, MARK_HEIGHT, UI_ACCENT);
}

static void circle(int cx, int cy, int radius, uint16_t color)
{
    int outer = radius * radius, inner = (radius - 2) * (radius - 2);
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            int distance = x * x + y * y;
            if (distance <= outer && distance >= inner) pixel(cx + x, cy + y, color);
        }
}

static void draw_diagnostics(const lab_display_state_t *state)
{
    enum { KEY_Y = UI_CONTENT_TOP + 20, KEY_HEIGHT = 72,
           KEY_WIDTH = (CONTENT_WIDTH - 3 * UI_SPACE) / 4,
           PLOT_TOP = KEY_Y + KEY_HEIGHT + 3 * UI_SPACE,
           PLOT_X = WIDTH - CONTENT_WIDTH / 4, PLOT_Y = (PLOT_TOP + CONTENT_BOTTOM) / 2,
           PLOT_RADIUS = 64 };
    const lab_touch_state_t *touch = &state->touch;
    bool joystick_ready = state->adc_ok && state->joystick_calibrated;

    page_title(app_title(state->app.current));
    char label[40];
    if (touch->ready)
        snprintf(label, sizeof(label), "Touch %u / Max %u", touch->reported, touch->max_reported);
    else
        snprintf(label, sizeof(label), "Touch error");
    text_end(WIDTH, UI_TITLE_BASELINE, label, UI_FONT_HINT, UI_TEXT);
    snprintf(label, sizeof(label), "Valid %u%s", touch->valid,
             touch->reported > LAB_TOUCH_POINTS ? " / Over two" : "");
    text_end(WIDTH, 50, label, UI_FONT_HINT, UI_SECONDARY);

    text_at(UI_LEFT_MARGIN, KEY_Y - 12, "Buttons", UI_FONT_HINT, UI_SECONDARY);
    const char *keys[] = {"SW1", "SW2", "SW3", "Stick"};
    for (unsigned i = 0; i < 4; ++i) {
        int x = UI_LEFT_MARGIN + (int)i * (KEY_WIDTH + UI_SPACE);
        bool pressed = state->buttons[i];
        rect(x, KEY_Y, KEY_WIDTH, KEY_HEIGHT, pressed ? UI_ENGAGED : UI_SURFACE);
        text_at(x + UI_INSET, KEY_Y + 32, keys[i], UI_FONT_BODY, UI_TEXT);
        text_at(x + UI_INSET, KEY_Y + 59, pressed ? "Pressed" : "Ready", UI_FONT_HINT,
                pressed ? UI_TEXT : UI_SECONDARY);
    }

    rect(UI_LEFT_MARGIN, PLOT_TOP, CONTENT_WIDTH, CONTENT_BOTTOM - PLOT_TOP, UI_SURFACE);
    text_at(CONTENT_X, PLOT_TOP + 36, "Joystick", UI_FONT_BODY, UI_TEXT);
    text_at(CONTENT_X, 297, "X", UI_FONT_HINT, UI_SECONDARY);
    text_at(CONTENT_X, 352, "Y", UI_FONT_HINT, UI_SECONDARY);
    snprintf(label, sizeof(label), "%d", state->normalized_x);
    text_at(CONTENT_X + 40, 297, joystick_ready ? label : "Wait", UI_FONT_TITLE, UI_TEXT);
    snprintf(label, sizeof(label), "%d", state->normalized_y);
    text_at(CONTENT_X + 40, 352, joystick_ready ? label : "Wait", UI_FONT_TITLE, UI_TEXT);

    circle(PLOT_X, PLOT_Y, PLOT_RADIUS, UI_GUIDE);
    rect(PLOT_X, PLOT_Y - PLOT_RADIUS + 1, 1, 2 * PLOT_RADIUS - 1, UI_GUIDE);
    rect(PLOT_X - PLOT_RADIUS + 1, PLOT_Y, 2 * PLOT_RADIUS - 1, 1, UI_GUIDE);
    if (joystick_ready) {
        int x = PLOT_X + state->normalized_x * 59 / 1000;
        int y = PLOT_Y - state->normalized_y * 59 / 1000;
        rect(x - 4, y - 4, UI_SPACE, UI_SPACE, UI_ACCENT);
    }
    footer(joystick_ready ? "Normalized / Deadzone 60" : "Waiting for stick", NULL, apps_hint);

    for (unsigned i = 0; i < touch->valid; ++i) {
        int x = touch->points[i].x, y = touch->points[i].y;
        uint16_t color = i == 0 ? UI_TEXT : UI_SECONDARY;
        circle(x, y, 22, color);
        rect(x - 2, y - 2, 5, 5, color);
    }
}

static void draw_selector(const app_view_t *app)
{
    enum { ROW_HEIGHT = (CONTENT_BOTTOM - UI_CONTENT_TOP - (JINTAI_APP_COUNT - 1) * UI_SPACE) / JINTAI_APP_COUNT };
    page_title("Apps");
    for (unsigned i = 0; i < JINTAI_APP_COUNT; ++i) {
        int y = UI_CONTENT_TOP + (int)i * (ROW_HEIGHT + UI_SPACE);
        int baseline = y + ROW_HEIGHT / 2 + 9;
        list_row(y, ROW_HEIGHT, i == app->selected);
        text_at(CONTENT_X, baseline, app_title(i), UI_FONT_BODY, UI_TEXT);
        if (i == app->current)
            text_end(WIDTH - UI_INSET, baseline, "Current app", UI_FONT_HINT, UI_SECONDARY);
    }
    footer("Release all three keys to switch", NULL, move_hint);
}

static void draw_mouse(const app_view_t *app)
{
    static const char *const buttons[][2] = {
        {"SW2", "Right click"}, {"SW3", "Left click"},
        {"SW1 + SW2", "Back"}, {"SW2 + SW3", "Forward"},
    };
    static const char *const gestures[][2] = {
        {"Drag", "Move"}, {"Tap", "Left click"},
        {"Two-finger drag", "Scroll"}, {"Two-finger tap", "Right click"},
    };
    page_title(app_title(app->current));
    rect(UI_LEFT_MARGIN, UI_CONTENT_TOP, CONTENT_WIDTH, 112, UI_SURFACE);
    text_at(CONTENT_X, UI_CONTENT_TOP + 38, "Current mode", UI_FONT_HINT, UI_SECONDARY);
    text_at(CONTENT_X, UI_CONTENT_TOP + 85, app->mouse_scroll ? "Scroll" : "Cursor", UI_FONT_TITLE, UI_TEXT);
    text_end(WIDTH - UI_INSET, UI_CONTENT_TOP + 85, "SW1: change mode", UI_FONT_HINT, UI_SECONDARY);

    text_at(UI_LEFT_MARGIN, 213, "Buttons", UI_FONT_BODY, UI_TEXT);
    text_at(328, 213, "Touchpad", UI_FONT_BODY, UI_TEXT);
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        int y = 251 + (int)i * 40;
        text_at(UI_LEFT_MARGIN, y, buttons[i][0], UI_FONT_HINT, UI_SECONDARY);
        text_at(166, y, buttons[i][1], UI_FONT_HINT, UI_TEXT);
        text_at(328, y, gestures[i][0], UI_FONT_HINT, UI_SECONDARY);
        text_end(WIDTH, y, gestures[i][1], UI_FONT_HINT, UI_TEXT);
    }
    footer(apps_hint, NULL, NULL);
}

static void codex_bitmap(int x, int y, const uint8_t *bitmap, unsigned height, uint16_t color)
{
    for (unsigned row = 0; row < height; ++row)
        for (unsigned col = 0; col < CODEX_TITLE_WIDTH; ++col)
            if (bitmap[row * (CODEX_TITLE_WIDTH / 8) + col / 8] & (0x80 >> (col % 8)))
                pixel(x + (int)col, y + (int)row, color);
}

static void status_stroke(int x, int y, int dx, int dy, int length, uint16_t color)
{
    for (int i = 0; i < length; ++i) rect(x + i * dx, y + i * dy, 2, 2, color);
}

static void status_spinner(int x, int y, int64_t now_ms)
{
    /* Dot masks for ⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏. */
    static const uint8_t frames[] = {0x0b, 0x19, 0x39, 0x38, 0x3c, 0x34, 0x26, 0x27, 0x07, 0x0f};
    static const uint8_t dots[][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
    uint8_t frame = frames[(now_ms / 120) % sizeof(frames)];
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 2; ++column)
            if (frame & dots[row][column]) rect(x - 4 + column * 6, y - 8 + row * 5, 3, 3, UI_SESSION_RUNNING);
}

static void codex_status(int right, int baseline, uint8_t status, int64_t now_ms)
{
    int x = right - 9, y = baseline - 9;
    switch (status) {
        case CODEX_RUNNING: status_spinner(x, y, now_ms); break;
        case CODEX_COMPLETED:
            status_stroke(x - 7, y, 1, 1, 5, UI_SESSION_DONE);
            status_stroke(x - 3, y + 4, 1, -1, 11, UI_SESSION_DONE);
            break;
        case CODEX_FAILED:
            status_stroke(x - 6, y - 6, 1, 1, 13, UI_SESSION_FAILED);
            status_stroke(x - 6, y + 6, 1, -1, 13, UI_SESSION_FAILED);
            break;
        default: status_stroke(x - 5, y, 1, 0, 11, UI_SECONDARY); break;
    }
}

static const char *codex_link_status(const lab_display_state_t *state)
{
    if (!state->codex_last_contact_ms) return "Waiting for Mac";
    if (state->codex_now_ms - state->codex_last_contact_ms > CODEX_TIMEOUT_MS) return "Mac is offline";
    return NULL;
}

static void draw_codex_detail(const lab_display_state_t *state)
{
    const codex_detail_t *detail = state->codex_detail;
    const char *notice = codex_link_status(state);
    if (!notice) {
        if (!detail || !detail->received_ms || state->codex_now_ms - detail->received_ms > CODEX_TIMEOUT_MS ||
            memcmp(detail->id, state->app.codex_id, CODEX_ID_BYTES) != 0) notice = "Loading session";
        else if (!detail->pages) notice = "Session unavailable";
    }
    if (notice) {
        page_title(app_title(state->app.current));
        text_at(UI_LEFT_MARGIN, UI_CONTENT_TOP + 53, notice, UI_FONT_BODY, UI_TEXT);
        footer("SW3: back", NULL, NULL);
        return;
    }

    codex_bitmap(UI_LEFT_MARGIN, 5, detail->session.title, CODEX_TITLE_HEIGHT, UI_TEXT);
    codex_status(WIDTH, UI_TITLE_BASELINE, detail->session.state, state->codex_now_ms);

    char page[24];
    snprintf(page, sizeof(page), "Page %u / %u", state->app.codex_page + 1, detail->pages);
    footer("SW3: back", detail->pages > 1 ? page : NULL, detail->pages > 1 ? move_hint : NULL);

    rect(UI_LEFT_MARGIN, UI_CONTENT_TOP, CONTENT_WIDTH, CODEX_DETAIL_HEIGHT + 2 * UI_SPACE, UI_SURFACE);
    if (detail->page != state->app.codex_page)
        text_at(CONTENT_X, UI_CONTENT_TOP + 53, "Loading page", UI_FONT_BODY, UI_TEXT);
    else if (!detail->has_reply) text_at(CONTENT_X, UI_CONTENT_TOP + 53, "No reply yet", UI_FONT_BODY, UI_TEXT);
    else codex_bitmap(CONTENT_X, UI_CONTENT_TOP + UI_SPACE, detail->reply, CODEX_DETAIL_HEIGHT, UI_TEXT);
}

static void draw_codex(const lab_display_state_t *state)
{
    if (state->app.codex_detail) {
        draw_codex_detail(state);
        return;
    }
    enum { FIRST_ROW = UI_CONTENT_TOP + UI_SPACE, VISIBLE_ROWS = 3,
           ROW_HEIGHT = (CONTENT_BOTTOM - FIRST_ROW - (VISIBLE_ROWS - 1) * UI_SPACE) / VISIBLE_ROWS };
    const codex_snapshot_t *snapshot = state->codex;

    page_title(app_title(state->app.current));
    const char *empty = codex_link_status(state);
    if (!empty) {
        if (!snapshot || !snapshot->received_ms || state->codex_now_ms - snapshot->received_ms > CODEX_TIMEOUT_MS)
            empty = "Loading sessions";
        else if (!snapshot->available) empty = "Session sources unavailable";
        else if (!snapshot->count) empty = "No active sessions";
    }
    if (empty) {
        text_at(UI_LEFT_MARGIN, UI_CONTENT_TOP + 53, empty, UI_FONT_BODY, UI_TEXT);
        footer(apps_hint, NULL, NULL);
        return;
    }

    unsigned focus = state->app.codex_row < snapshot->count ? state->app.codex_row : snapshot->count - 1;
    for (unsigned i = 0; i < snapshot->count; ++i)
        if (memcmp(state->app.codex_id, snapshot->rows[i].id, CODEX_ID_BYTES) == 0) focus = i;
    char label[24];
    snprintf(label, sizeof(label), "%u/%u", focus + 1, snapshot->count);
    text_end(WIDTH, UI_TITLE_BASELINE, label, UI_FONT_HINT, UI_SECONDARY);

    unsigned first = focus >= VISIBLE_ROWS ? focus - (VISIBLE_ROWS - 1) : 0;
    for (unsigned i = first; i < snapshot->count && i < first + VISIBLE_ROWS; ++i) {
        int y = FIRST_ROW + (int)(i - first) * (ROW_HEIGHT + UI_SPACE);
        const codex_row_t *row = &snapshot->rows[i];
        int title_y = y + (ROW_HEIGHT - CODEX_TITLE_HEIGHT - CODEX_PREVIEW_HEIGHT - UI_SPACE) / 2;
        list_row(y, ROW_HEIGHT, focus == i);
        codex_bitmap(CONTENT_X, title_y, row->session.title, CODEX_TITLE_HEIGHT, UI_TEXT);
        codex_status(WIDTH - UI_INSET, title_y + 25, row->session.state, state->codex_now_ms);
        if (row->has_preview)
            codex_bitmap(CONTENT_X, title_y + CODEX_TITLE_HEIGHT + UI_SPACE, row->preview, CODEX_PREVIEW_HEIGHT, UI_SECONDARY);
    }
    footer("SW2: details", NULL, snapshot->count > 1 ? move_hint : NULL);
}

static void draw_placeholder(const app_view_t *app)
{
    page_title(app_title(app->current));
    rect(UI_LEFT_MARGIN, UI_CONTENT_TOP, CONTENT_WIDTH, 160, UI_SURFACE);
    text_at(CONTENT_X, UI_CONTENT_TOP + 38, "Button presses", UI_FONT_BODY, UI_TEXT);
    char count[20];
    snprintf(count, sizeof(count), "%u", app->presses);
    text_at(CONTENT_X, UI_CONTENT_TOP + 92, count, UI_FONT_TITLE, UI_TEXT);
    text_at(CONTENT_X, UI_CONTENT_TOP + 135, "Resets when you switch apps", UI_FONT_HINT, UI_SECONDARY);
    footer(apps_hint, NULL, NULL);
}

void app_ui_sleep_warning(uint16_t *buffer)
{
    begin_frame(buffer);
    const char *label = "Zzz...";
    text_at((WIDTH - text_width(label, UI_FONT_TITLE)) / 2, HEIGHT / 2 + 12, label, UI_FONT_TITLE, UI_TEXT);
}

void app_ui_render(uint16_t *buffer, const lab_display_state_t *state)
{
    begin_frame(buffer);
    if (state->app.choosing) {
        draw_selector(&state->app);
        return;
    }
    switch (state->app.current) {
        case 0: draw_diagnostics(state); break;
        case 1: draw_mouse(&state->app); break;
        case JINTAI_CODEX_APP: draw_codex(state); break;
        default: draw_placeholder(&state->app); break;
    }
}
