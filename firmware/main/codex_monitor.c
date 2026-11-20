#include "codex_monitor.h"
#include <string.h>

static const uint8_t magic[] = {'J', 'C', 'X', '4'};

typedef struct {
    const uint8_t *at;
    const uint8_t *end;
} reader_t;

static unsigned little_endian_16(const uint8_t *bytes)
{
    return bytes[0] | (unsigned)bytes[1] << 8;
}

static uint32_t little_endian_32(const uint8_t *bytes)
{
    return little_endian_16(bytes) | (uint32_t)little_endian_16(bytes + 2) << 16;
}

static uint32_t crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320U : 0);
    }
    return ~crc;
}

static bool read_bitmap(reader_t *reader, uint8_t *output, size_t size, bool *present)
{
    if (reader->end - reader->at < 2) return false;
    unsigned packed = little_endian_16(reader->at);
    reader->at += 2;
    if ((size_t)(reader->end - reader->at) < packed) return false;
    if (present) *present = packed != 0;
    if (!packed) return present != NULL;

    const uint8_t *end = reader->at + packed;
    while (reader->at < end) {
        uint8_t token = *reader->at++;
        size_t count = (token & 0x7f) + 1;
        if (count > size) return false;
        if (token & 0x80) memset(output, 0, count);
        else {
            if ((size_t)(end - reader->at) < count) return false;
            memcpy(output, reader->at, count);
            reader->at += count;
        }
        output += count;
        size -= count;
    }
    return size == 0;
}

static bool read_session(reader_t *reader, codex_session_t *session)
{
    if (reader->at == reader->end) return false;
    session->state = *reader->at++;
    if (session->state < CODEX_RUNNING || session->state > CODEX_CANCELLED) return false;
    return read_bitmap(reader, session->title, CODEX_TITLE_BYTES, NULL);
}

static bool read_list(reader_t *reader, codex_snapshot_t *list)
{
    if (reader->end - reader->at < 2) return false;
    unsigned available = *reader->at++;
    unsigned count = *reader->at++;
    if (available > 1 || count > CODEX_MAX_ROWS || (!available && count)) return false;
    for (unsigned i = 0; i < count; ++i) {
        codex_row_t *row = &list->rows[i];
        if (reader->end - reader->at < CODEX_ID_BYTES) return false;
        memcpy(row->id, reader->at, CODEX_ID_BYTES);
        reader->at += CODEX_ID_BYTES;
        if (!read_session(reader, &row->session) ||
            !read_bitmap(reader, row->preview, CODEX_PREVIEW_BYTES, &row->has_preview)) return false;
    }
    list->available = available != 0;
    list->count = count;
    return reader->at == reader->end;
}

static bool read_detail(reader_t *reader, codex_detail_t *detail)
{
    if (reader->end - reader->at < CODEX_ID_BYTES + 2) return false;
    memcpy(detail->id, reader->at, CODEX_ID_BYTES);
    reader->at += CODEX_ID_BYTES;
    detail->page = *reader->at++;
    detail->pages = *reader->at++;
    if (!detail->pages) return reader->at == reader->end;
    if (detail->pages > CODEX_MAX_PAGES || detail->page >= detail->pages) return false;
    return read_session(reader, &detail->session) &&
        read_bitmap(reader, detail->reply, CODEX_DETAIL_BYTES, &detail->has_reply) && reader->at == reader->end;
}

codex_frame_t codex_decoder_byte(codex_decoder_t *decoder, uint8_t byte,
                                 codex_snapshot_t *list, codex_detail_t *detail, uint32_t *clock_ms)
{
    if (decoder->used < sizeof(magic) && byte != magic[decoder->used]) {
        decoder->used = byte == magic[0] ? 1 : 0;
        return CODEX_FRAME_NONE;
    }
    decoder->data[decoder->used++] = byte;
    const size_t header = sizeof(magic) + 3;
    const uint8_t *body = decoder->data + sizeof(magic);
    if (decoder->used == header) {
        unsigned length = little_endian_16(body + 1);
        bool valid = body[0] == CODEX_FRAME_CLOCK ? length == 4 :
                     (body[0] == CODEX_FRAME_LIST || body[0] == CODEX_FRAME_DETAIL) && length <= CODEX_MAX_PAYLOAD;
        if (!valid) {
            decoder->used = byte == magic[0] ? 1 : 0;
            return CODEX_FRAME_NONE;
        }
        decoder->expected = header + length + 4;
    }
    if (decoder->used < header || decoder->used != decoder->expected) return CODEX_FRAME_NONE;
    decoder->used = 0;
    const uint8_t *checksum = decoder->data + decoder->expected - 4;
    uint32_t expected = little_endian_32(checksum);
    if (crc32(body, checksum - body) != expected) return CODEX_FRAME_NONE;
    if (body[0] == CODEX_FRAME_CLOCK) {
        uint32_t value = little_endian_32(body + 3);
        if (value >= CLAWDOCK_DAY_MS) return CODEX_FRAME_NONE;
        *clock_ms = value;
        return CODEX_FRAME_CLOCK;
    }
    reader_t reader = { .at = body + 3, .end = checksum };
    if (body[0] == CODEX_FRAME_LIST)
        return read_list(&reader, list) ? CODEX_FRAME_LIST : CODEX_FRAME_NONE;
    return read_detail(&reader, detail) ? CODEX_FRAME_DETAIL : CODEX_FRAME_NONE;
}
