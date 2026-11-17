#include <assert.h>
#include <stdio.h>
#include "joystick.h"

int main(void)
{
    lab_joystick_cal_t cal = {0};
    for (int i = 0; i < 24; ++i) lab_joystick_update(&cal, 1815, 1734);
    assert(!cal.ready);
    lab_joystick_update(&cal, 1815, 1734);
    assert(cal.ready && cal.center_x == 1815 && cal.center_y == 1734);
    assert(lab_joystick_normalize(1815, 1815, 1815, 1815) == 0);
    assert(lab_joystick_normalize(1755, 1815, 1815, 1815) == 0);
    assert(lab_joystick_normalize(1875, 1815, 1815, 1815) == 0);
    assert(lab_joystick_normalize(1754, 1815, 1815, 1815) == -2);
    assert(lab_joystick_normalize(1876, 1815, 1815, 1815) == 2);
    assert(lab_joystick_normalize(1915, 1815, 1815, 1815) == 90);
    lab_joystick_update(&cal, 551, 408);
    lab_joystick_update(&cal, 3026, 2804);
    assert(lab_joystick_normalize(1815, cal.center_x, cal.min_x, cal.max_x) == 0);
    assert(lab_joystick_normalize(551, cal.center_x, cal.min_x, cal.max_x) == -1000);
    assert(lab_joystick_normalize(3026, cal.center_x, cal.min_x, cal.max_x) == 1000);
    assert(lab_joystick_normalize(408, cal.center_y, cal.min_y, cal.max_y) == -1000);
    assert(lab_joystick_normalize(2804, cal.center_y, cal.min_y, cal.max_y) == 1000);
    assert(lab_joystick_normalize(4000, cal.center_x, cal.min_x, cal.max_x) == 1000);
    lab_joystick_cal_t moving = {0};
    lab_joystick_update(&moving, 1815, 1734);
    lab_joystick_update(&moving, 2000, 1734);
    assert(!moving.ready && moving.samples == 0);
    puts("joystick calibration tests passed");
    return 0;
}
