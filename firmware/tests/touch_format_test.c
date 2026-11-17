#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "touch_format.h"

int main(void)
{
    lab_touch_state_t state = {0};
    uint8_t raw[LAB_TOUCH_RAW_BYTES] = {2, 0x00, 100, 0x10, 200, 0, 0,
                                       0x81, 144, 0x22, 88, 0, 0};
    lab_touch_decode(&state, raw);
    assert(state.ready && state.reported == 2 && state.valid == 2 && state.max_reported == 2);
    assert(state.points[0].x == 439 && state.points[0].y == 100 && state.points[0].id == 1);
    assert(state.points[1].x == 39 && state.points[1].y == 400 && state.points[1].id == 2);
    assert(memcmp(state.raw, raw, sizeof(raw)) == 0);

    raw[0] = 3; /* The full reported count is retained even though only two slots are parsed. */
    raw[1] = 0x40; /* First record is a release. */
    raw[7] = 0x8f; /* Second record has an out-of-range panel X. */
    lab_touch_decode(&state, raw);
    assert(state.reported == 3 && state.valid == 0 && state.max_reported == 3);
    raw[0] = 1;
    raw[1] = 0x81; raw[2] = 0xe0; /* x=480 at the edge */
    raw[3] = 0x02; raw[4] = 0x80; /* y=640 at the edge */
    lab_touch_decode(&state, raw);
    assert(state.valid == 1 && state.points[0].x == 0 && state.points[0].y == 479);
    raw[2] = 0xe1; /* x=481 is outside the documented panel bounds. */
    lab_touch_decode(&state, raw);
    assert(state.valid == 0);
    raw[0] = 16; /* Keep all eight bits; unexpected counts must stay visible. */
    lab_touch_decode(&state, raw);
    assert(state.reported == 16 && state.max_reported == 16);
    raw[0] = 0;
    lab_touch_decode(&state, raw);
    assert(state.reported == 0 && state.valid == 0 && state.max_reported == 16);
    puts("touch format tests passed");
    return 0;
}
