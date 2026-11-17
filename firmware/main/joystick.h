#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Calibrate the center only after a short, stable startup window. Extents are
 * learned independently in each direction for this boot, without flash writes. */
typedef struct {
    bool ready;
    int center_x, center_y;
    int min_x, max_x, min_y, max_y;
    int64_t sum_x, sum_y;
    int window_min_x, window_max_x, window_min_y, window_max_y;
    unsigned int samples;
} lab_joystick_cal_t;

void lab_joystick_update(lab_joystick_cal_t *cal, int x, int y);
int lab_joystick_normalize(int value, int center, int min, int max);
