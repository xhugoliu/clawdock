#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LAB_TOUCH_POINTS 2
#define LAB_TOUCH_RAW_BYTES 13

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t id;
} lab_touch_point_t;

typedef struct {
    bool ready;
    uint8_t reported;
    uint8_t valid;
    uint8_t max_reported;
    uint32_t errors;
    uint8_t raw[LAB_TOUCH_RAW_BYTES];
    lab_touch_point_t points[LAB_TOUCH_POINTS];
} lab_touch_state_t;

void lab_touch_decode(lab_touch_state_t *state, const uint8_t raw[LAB_TOUCH_RAW_BYTES]);
