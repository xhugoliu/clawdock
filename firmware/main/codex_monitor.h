#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CODEX_MAX_ROWS 6
#define CODEX_TITLE_WIDTH 544
#define CODEX_TITLE_HEIGHT 32
#define CODEX_PREVIEW_HEIGHT 32
#define CODEX_DETAIL_HEIGHT 320
#define CODEX_TITLE_BYTES (CODEX_TITLE_WIDTH / 8 * CODEX_TITLE_HEIGHT)
#define CODEX_PREVIEW_BYTES (CODEX_TITLE_WIDTH / 8 * CODEX_PREVIEW_HEIGHT)
#define CODEX_DETAIL_BYTES (CODEX_TITLE_WIDTH / 8 * CODEX_DETAIL_HEIGHT)
#define CODEX_ID_BYTES 16
#define CODEX_TIMEOUT_MS 8000
#define CODEX_MAX_PAGES 24
#define CODEX_MAX_PAYLOAD 27000
#define CODEX_FRAME_BYTES (4 + 3 + CODEX_MAX_PAYLOAD + 4)
#define CLAWDOCK_DAY_MS 86400000U

typedef enum {
    CODEX_RUNNING = 1,
    CODEX_COMPLETED,
    CODEX_FAILED,
    CODEX_CANCELLED,
} codex_state_t;

typedef struct {
    uint8_t state;
    uint8_t title[CODEX_TITLE_BYTES];
} codex_session_t;

typedef struct {
    uint8_t id[CODEX_ID_BYTES];
    codex_session_t session;
    bool has_preview;
    uint8_t preview[CODEX_PREVIEW_BYTES];
} codex_row_t;

typedef struct {
    int64_t received_ms;
    bool available;
    unsigned count;
    codex_row_t rows[CODEX_MAX_ROWS];
} codex_snapshot_t;

typedef struct {
    int64_t received_ms;
    uint8_t id[CODEX_ID_BYTES];
    uint8_t page;
    uint8_t pages;
    codex_session_t session;
    bool has_reply;
    uint8_t reply[CODEX_DETAIL_BYTES];
} codex_detail_t;

typedef struct {
    int64_t received_ms;
    bool sources_unavailable;
    unsigned count;
    unsigned detail_pages;
    uint8_t ids[CODEX_MAX_ROWS][CODEX_ID_BYTES];
    uint8_t states[CODEX_MAX_ROWS];
} codex_summary_t;

static inline bool codex_summary_is_current(const codex_summary_t *sessions, int64_t now_ms)
{
    return sessions && sessions->received_ms && !sessions->sources_unavailable &&
           now_ms - sessions->received_ms <= CODEX_TIMEOUT_MS;
}

typedef struct {
    size_t used;
    size_t expected;
    uint8_t data[CODEX_FRAME_BYTES];
} codex_decoder_t;

typedef enum {
    CODEX_FRAME_NONE,
    CODEX_FRAME_LIST,
    CODEX_FRAME_DETAIL,
    CODEX_FRAME_CLOCK,
} codex_frame_t;

codex_frame_t codex_decoder_byte(codex_decoder_t *decoder, uint8_t byte,
                                 codex_snapshot_t *list, codex_detail_t *detail, uint32_t *clock_ms);
