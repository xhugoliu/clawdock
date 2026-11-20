#include "app_ui.h"
#include "app_ui_theme.h"
#include "sessions_clawd.h"
#include "miniz.h"
#include <stdio.h>
#include <string.h>

#define WIDTH UI_WIDTH
#define HEIGHT UI_HEIGHT
#define CONTENT_WIDTH (WIDTH - UI_LEFT_MARGIN)
#define CONTENT_X (UI_LEFT_MARGIN + UI_INSET)
#define FOOTER_BASELINE (HEIGHT - UI_FOOTER_DESCENT)
#define CONTENT_BOTTOM (FOOTER_BASELINE - 30)

static uint16_t *fb; /* Used only by the display task. */
static sessions_clawd_t sessions_clawd;
static unsigned sessions_clawd_requests;
static const char apps_hint[] = "Apps: hold all 3 keys";
static const char move_hint[] = "Stick: up / down";
static const char pet_hint[] = "SW1/2: pet";

extern const uint8_t ui_font_data[] __asm__("_binary_app_ui_font_bin_start");
extern const uint8_t clawd_frames[] __asm__("_binary_clawd_frames_bin_start");

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

static void list_row(int y, int height, int focus_y, bool focused)
{
    enum { MARK_WIDTH = 3, MARK_HEIGHT = 24 };
    rect(UI_LEFT_MARGIN, y, CONTENT_WIDTH, height, focused ? UI_ENGAGED : UI_SURFACE);
    if (focused) rect(UI_LEFT_MARGIN, focus_y - MARK_HEIGHT / 2, MARK_WIDTH, MARK_HEIGHT, UI_ACCENT);
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
        list_row(y, ROW_HEIGHT, y + ROW_HEIGHT / 2, i == app->selected);
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

static bool codex_is_offline(const lab_display_state_t *state)
{
    return state->codex_last_contact_ms && state->now_ms - state->codex_last_contact_ms > CODEX_TIMEOUT_MS;
}

static const char *codex_link_status(const lab_display_state_t *state)
{
    if (!state->codex_last_contact_ms) return "(Waiting for Mac)";
    if (codex_is_offline(state)) return "(Mac is offline)";
    if (state->codex_sources_unavailable) return "(Sources unavailable)";
    return NULL;
}

enum {
    SESSIONS_FIRST_ROW = UI_CONTENT_TOP + UI_SPACE,
    SESSIONS_VISIBLE_ROWS = 3,
    SESSION_ROW_HEIGHT = (CONTENT_BOTTOM - SESSIONS_FIRST_ROW - (SESSIONS_VISIBLE_ROWS - 1) * UI_SPACE) / SESSIONS_VISIBLE_ROWS,
    SESSIONS_ROWS_HEIGHT = SESSIONS_VISIBLE_ROWS * SESSION_ROW_HEIGHT + (SESSIONS_VISIBLE_ROWS - 1) * UI_SPACE,
    SESSION_TITLE_OFFSET = (SESSION_ROW_HEIGHT - CODEX_TITLE_HEIGHT - CODEX_PREVIEW_HEIGHT - UI_SPACE) / 2,
    SESSION_REPLY_OFFSET = SESSION_TITLE_OFFSET + CODEX_TITLE_HEIGHT + UI_SPACE,
    SESSION_REPLY_BASELINE = CODEX_PREVIEW_HEIGHT - 6, // Matches the Mac's reply bitmaps.
};

static const char *codex_reply_placeholder(const codex_session_t *session)
{
    return session->state == CODEX_RUNNING ? "Waiting for a reply" : "No reply available";
}

static void draw_codex_position(unsigned focus, unsigned count)
{
    char label[24];
    snprintf(label, sizeof(label), "%u/%u", focus + 1, count);
    text_end(WIDTH, UI_TITLE_BASELINE, label, UI_FONT_HINT, UI_SECONDARY);
}

static void draw_codex_card(const lab_display_state_t *state, int y, int height, bool focused,
                            const codex_session_t *session)
{
    int title_y = y + SESSION_TITLE_OFFSET;
    list_row(y, height, title_y + CODEX_TITLE_HEIGHT + UI_SPACE / 2, focused);
    if (session) {
        int64_t spinner_ms = codex_is_offline(state) ? state->codex_last_contact_ms + CODEX_TIMEOUT_MS : state->now_ms;
        codex_bitmap(CONTENT_X, title_y, session->title, CODEX_TITLE_HEIGHT, UI_TEXT);
        codex_status(WIDTH - UI_INSET, title_y + 25, session->state, spinner_ms);
    }
}

static void draw_codex_detail(const lab_display_state_t *state, const codex_row_t *row, const char *status)
{
    const codex_detail_t *detail = state->codex_detail;
    const codex_session_t *session = row ? &row->session : NULL;
    const uint8_t *reply = row && row->has_preview ? row->preview : NULL;
    int height = CODEX_PREVIEW_HEIGHT;
    const char *notice = !session && !status ? "Loading session" : NULL;
    unsigned pages = 0, page = 0;
    if (detail && detail->received_ms && memcmp(detail->id, state->app.codex_id, CODEX_ID_BYTES) == 0) {
        if (detail->pages) {
            session = &detail->session;
            pages = detail->pages;
            page = detail->page;
            const int space = SESSIONS_ROWS_HEIGHT - SESSION_REPLY_OFFSET;
            height = space < CODEX_DETAIL_HEIGHT ? space : CODEX_DETAIL_HEIGHT;
            reply = detail->has_reply ? detail->reply : NULL;
            notice = NULL;
        } else if (!status) {
            notice = "Session unavailable";
        }
    }

    const int reply_y = SESSIONS_FIRST_ROW + SESSION_REPLY_OFFSET;
    draw_codex_card(state, SESSIONS_FIRST_ROW, SESSIONS_ROWS_HEIGHT, true, session);
    if (!notice && !reply && session) notice = codex_reply_placeholder(session);
    if (notice) {
        int baseline = session ? reply_y + SESSION_REPLY_BASELINE : SESSIONS_FIRST_ROW + SESSION_TITLE_OFFSET + 25;
        text_at(CONTENT_X, baseline, notice, UI_FONT_BODY, UI_SECONDARY);
    } else if (reply) codex_bitmap(CONTENT_X, reply_y, reply, height, UI_SECONDARY);

    char navigation[48];
    if (pages > 1) snprintf(navigation, sizeof(navigation), "Page %u / %u  %s", page + 1, pages, move_hint);
    footer(pet_hint, pages > 1 ? navigation : NULL, "SW3: preview");
}

static void draw_codex(const lab_display_state_t *state)
{
    const codex_snapshot_t *snapshot = state->codex;
    unsigned count = snapshot && snapshot->available ? snapshot->count : 0;
    unsigned focus = 0;
    while (focus < count && memcmp(state->app.codex_id, snapshot->rows[focus].id, CODEX_ID_BYTES) != 0)
        ++focus;

    const char *title = app_title(JINTAI_CODEX_APP);
    const char *status = codex_link_status(state);
    page_title(title);
    if (status) text_at(UI_LEFT_MARGIN + text_width(title, UI_FONT_TITLE) + UI_INSET,
                        UI_TITLE_BASELINE, status, UI_FONT_HINT, UI_SECONDARY);
    if (state->app.codex_detail) {
        const codex_row_t *row = focus < count ? &snapshot->rows[focus] : NULL;
        if (row) draw_codex_position(focus, count);
        draw_codex_detail(state, row, status);
        return;
    }
    if (!count) {
        if (!status && (!snapshot || !snapshot->received_ms))
            text_at(UI_LEFT_MARGIN, UI_CONTENT_TOP + 53, "Loading sessions", UI_FONT_BODY, UI_TEXT);
        footer(pet_hint, NULL, "Stick: left / right");
        return;
    }

    if (focus == count) focus = state->app.codex_row < count ? state->app.codex_row : count - 1;
    draw_codex_position(focus, count);

    unsigned first = focus >= SESSIONS_VISIBLE_ROWS ? focus - (SESSIONS_VISIBLE_ROWS - 1) : 0;
    for (unsigned i = first; i < count && i < first + SESSIONS_VISIBLE_ROWS; ++i) {
        int y = SESSIONS_FIRST_ROW + (int)(i - first) * (SESSION_ROW_HEIGHT + UI_SPACE);
        const codex_row_t *row = &snapshot->rows[i];
        draw_codex_card(state, y, SESSION_ROW_HEIGHT, focus == i, &row->session);
        const int reply_y = y + SESSION_REPLY_OFFSET;
        if (row->has_preview)
            codex_bitmap(CONTENT_X, reply_y, row->preview, CODEX_PREVIEW_HEIGHT, UI_SECONDARY);
        else
            text_at(CONTENT_X, reply_y + SESSION_REPLY_BASELINE, codex_reply_placeholder(&row->session),
                    UI_FONT_BODY, UI_SECONDARY);
    }
    footer(pet_hint, "SW3: details", snapshot->count > 1 ? "Stick: move / browse" : "Stick: left / right");
}

static unsigned read_u16(const uint8_t *bytes)
{
    return bytes[0] | (unsigned)bytes[1] << 8;
}

typedef struct { int left, top, right, bottom; } sprite_area_t;

enum { CLAWD_RUN_BYTES = 4, CLAWD_CLIP_BYTES = 6, CLAWD_FRAME_BYTES = 12 };
enum {
    CLAWD_SOURCE_BODY_COLOR = UI_RGB565(222, 136, 109),
    CLAWD_PANEL_BODY_COLOR = UI_RGB565(230, 119, 77),
};
static uint8_t clawd_runs[24 * 1024];

static size_t decode_clawd_runs(uint32_t offset)
{
    static tinfl_decompressor decoder;
    static uint32_t cached_offset;
    static size_t cached_size;
    if (cached_size && offset == cached_offset) return cached_size;

    const uint8_t *block = clawd_frames + offset;
    size_t input_size = read_u16(block), output_size = sizeof(clawd_runs);
    tinfl_init(&decoder);
    tinfl_status result = tinfl_decompress(&decoder, block + 2, &input_size, clawd_runs, clawd_runs, &output_size,
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    cached_offset = offset;
    cached_size = result == TINFL_STATUS_DONE && output_size % CLAWD_RUN_BYTES == 0 ? output_size : 0;
    return cached_size;
}

static void sprite_span(const sprite_area_t *area, int x, int y, int width,
                        uint16_t color, unsigned alpha)
{
    if (y < area->top || y >= area->bottom) return;
    int left = x < area->left ? area->left : x;
    int right = x + width > area->right ? area->right : x + width;
    if (left >= right) return;
    if (alpha == 15) {
        rect(left, y, right - left, 1, color);
        return;
    }
    for (int col = left; col < right; ++col) {
        uint16_t *target = fb + (size_t)col * HEIGHT + HEIGHT - 1 - y;
        *target = blend(*target, color, alpha);
    }
}

static int ceil_div(int value, int divisor)
{
    return value / divisor + (value % divisor > 0);
}

static const uint8_t *clawd_frame(unsigned index)
{
    return clawd_frames + CLAWD_POSE_COUNT * CLAWD_CLIP_BYTES + index * CLAWD_FRAME_BYTES;
}

static bool clawd_mirrored(const clawd_view_t *pet)
{
    /* The walking source looks left. The other clips use the default orientation. */
    return pet->face_left != (pet->pose == CLAWD_WALKING);
}

static int fit_clawd_x(const clawd_view_t *pet, int pixel_step, int64_t duration_ms, const sprite_area_t *area)
{
    const uint8_t *clip = clawd_frames + pet->pose * CLAWD_CLIP_BYTES;
    unsigned first = read_u16(clip), count = read_u16(clip + 2), interval = read_u16(clip + 4);
    if (duration_ms > 0 && duration_ms < (int64_t)count * interval)
        count = (duration_ms + interval - 1) / interval;

    bool mirrored = clawd_mirrored(pet);
    int min_left = 0, max_right = 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *frame = clawd_frame(first + i);
        int left = (int16_t)read_u16(frame + 4), width = read_u16(frame + 8);
        if (mirrored) left = -left - width;
        if (left < min_left) min_left = left;
        if (left + width > max_right) max_right = left + width;
    }

    int min_x = area->left - ceil_div(min_left, pixel_step);
    int max_x = area->right - ceil_div(max_right, pixel_step);
    if (pet->x < min_x) return min_x;
    if (pet->x > max_x) return max_x;
    return pet->x;
}

static void draw_clawd_sprite(const clawd_view_t *pet, int x, int y, int pixel_step, int64_t now_ms,
                              const sprite_area_t *area, uint16_t body_color)
{
    const uint8_t *clip = clawd_frames + pet->pose * CLAWD_CLIP_BYTES;
    int64_t elapsed = now_ms > pet->pose_started_ms ? now_ms - pet->pose_started_ms : 0;
    unsigned index = read_u16(clip) + elapsed / read_u16(clip + 4) % read_u16(clip + 2);
    const uint8_t *frame = clawd_frame(index);
    uint32_t data_offset = read_u16(frame) | (uint32_t)read_u16(frame + 2) << 16;
    int left = (int16_t)read_u16(frame + 4), top = (int16_t)read_u16(frame + 6);
    int width = read_u16(frame + 8), height = read_u16(frame + 10);
    bool mirrored = clawd_mirrored(pet);
    if (mirrored) left = -left - width;

    size_t decoded_size = decode_clawd_runs(data_offset);
    int row = 0, col = 0;
    for (const uint8_t *run = clawd_runs; row < height && run < clawd_runs + decoded_size; run += CLAWD_RUN_BYTES) {
        unsigned alpha = run[1];
        uint16_t color = read_u16(run + 2);
        if (color == CLAWD_SOURCE_BODY_COLOR) color = body_color;
        int remaining = run[0];
        while (remaining && row < height) {
            int count = remaining < width - col ? remaining : width - col;
            int dy = top + row;
            if (alpha && dy % pixel_step == 0) {
                int start = left + (mirrored ? width - col - count : col);
                int first = ceil_div(start, pixel_step), after = ceil_div(start + count, pixel_step);
                sprite_span(area, x + first, y + dy / pixel_step, after - first, color, alpha);
            }
            remaining -= count;
            col += count;
            if (col == width) {
                col = 0;
                ++row;
            }
        }
    }
}

static void draw_clawd(const lab_display_state_t *state, uint16_t body_color)
{
    const sprite_area_t stage = {UI_LEFT_MARGIN, UI_CONTENT_TOP - UI_INSET, WIDTH, FOOTER_BASELINE - 56};
    const clawd_view_t *pet = &state->app.clawd;
    char clock[] = "--:--";
    if (state->local_minute >= 0 && state->local_minute < 24 * 60) {
        int hour = state->local_minute / 60, minute = state->local_minute % 60;
        clock[0] = '0' + hour / 10;
        clock[1] = '0' + hour % 10;
        clock[3] = '0' + minute / 10;
        clock[4] = '0' + minute % 10;
    }
    page_title(app_title(JINTAI_CLAWD_APP));
    text_end(WIDTH - UI_INSET, UI_TITLE_BASELINE, clock, UI_FONT_TITLE, UI_TEXT);
    draw_clawd_sprite(pet, pet->x, pet->y, 1, state->now_ms, &stage, body_color);
    text_at(UI_LEFT_MARGIN, FOOTER_BASELINE - 32, "Stick: move", UI_FONT_HINT, UI_SECONDARY);
    footer("SW1-SW3 / tap Clawd: action", NULL,
           pet->pose == CLAWD_SLEEPING ? "Stick press: wake" : "Stick press: sleep");
}

void app_ui_sleep_warning(uint16_t *buffer)
{
    sessions_clawd_pause(&sessions_clawd);
    begin_frame(buffer);
    const char *label = "Zzz...";
    text_at((WIDTH - text_width(label, UI_FONT_TITLE)) / 2, HEIGHT / 2 + 12, label, UI_FONT_TITLE, UI_TEXT);
}

static void render(uint16_t *buffer, const lab_display_state_t *state, uint16_t clawd_body_color)
{
    bool pet_requested = sessions_clawd_requests != state->app.sessions_clawd_requests;
    sessions_clawd_requests = state->app.sessions_clawd_requests;
    begin_frame(buffer);
    if (state->app.choosing) {
        sessions_clawd_pause(&sessions_clawd);
        draw_selector(&state->app);
        return;
    }
    switch (state->app.current) {
        case JINTAI_INPUT_APP: draw_diagnostics(state); break;
        case JINTAI_MOUSE_APP: draw_mouse(&state->app); break;
        case JINTAI_CODEX_APP: draw_codex(state); break;
        case JINTAI_CLAWD_APP: draw_clawd(state, clawd_body_color); break;
    }
    if (state->app.current == JINTAI_CODEX_APP) {
        enum { CAMEO_PIXEL_STEP = 2 };
        const sprite_area_t stage = {UI_LEFT_MARGIN, UI_CONTENT_TOP, WIDTH, CONTENT_BOTTOM};
        sessions_clawd_update(&sessions_clawd, state->codex, pet_requested, state->app.sessions_clawd_stick_x, state->now_ms);
        const clawd_view_t *pet = &sessions_clawd.view;
        int64_t duration = pet->pose == CLAWD_IDLE ? sessions_clawd.patrol_ends_ms - pet->pose_started_ms : 0;
        int x = fit_clawd_x(pet, CAMEO_PIXEL_STEP, duration, &stage);
        draw_clawd_sprite(pet, x, CONTENT_BOTTOM - 12, CAMEO_PIXEL_STEP, state->now_ms,
                          &stage, clawd_body_color);
    } else {
        sessions_clawd_pause(&sessions_clawd);
    }
}

void app_ui_render(uint16_t *buffer, const lab_display_state_t *state)
{
    render(buffer, state, CLAWD_SOURCE_BODY_COLOR);
}

void app_ui_render_for_panel(uint16_t *buffer, const lab_display_state_t *state)
{
    render(buffer, state, CLAWD_PANEL_BODY_COLOR);
}
