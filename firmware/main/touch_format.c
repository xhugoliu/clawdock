#include "touch_format.h"
#include <string.h>

void lab_touch_decode(lab_touch_state_t *state, const uint8_t raw[LAB_TOUCH_RAW_BYTES])
{
    state->ready = true;
    memcpy(state->raw, raw, LAB_TOUCH_RAW_BYTES);
    state->reported = raw[0];
    if (state->reported > state->max_reported) state->max_reported = state->reported;
    state->valid = 0;
    for (unsigned i = 0; i < state->reported && i < LAB_TOUCH_POINTS; ++i) {
        const uint8_t *p = raw + 1 + i * 6;
        unsigned event = p[0] >> 6;
        unsigned panel_x = ((unsigned)(p[0] & 0x0f) << 8) | p[1];
        unsigned panel_y = ((unsigned)(p[2] & 0x0f) << 8) | p[3];
        if (event == 1 || event == 3 || panel_x > 480 || panel_y > 640) continue;
        /* X=480 was observed at an edge; accept both inclusive panel extents. */
        if (panel_x == 480) panel_x = 479;
        if (panel_y == 640) panel_y = 639;
        /* CST826 physical mounting on this device; inverse of the panel rotation. */
        lab_touch_point_t *point = &state->points[state->valid++];
        point->x = 639 - panel_y;
        point->y = panel_x;
        point->id = p[2] >> 4;
    }
}
