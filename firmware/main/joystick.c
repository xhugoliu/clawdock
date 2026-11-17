#include "joystick.h"

#define CENTER_SAMPLES 25
#define MAX_WINDOW_SPREAD 80
#define MIN_SPAN 500
#define DEADZONE_ADC 60

void lab_joystick_update(lab_joystick_cal_t *cal, int x, int y)
{
    if (!cal || x < 0 || x > 4095 || y < 0 || y > 4095) return;
    if (!cal->ready) {
        if (cal->samples == 0) {
            cal->window_min_x = cal->window_max_x = x;
            cal->window_min_y = cal->window_max_y = y;
        }
        if (x < cal->window_min_x) cal->window_min_x = x;
        if (x > cal->window_max_x) cal->window_max_x = x;
        if (y < cal->window_min_y) cal->window_min_y = y;
        if (y > cal->window_max_y) cal->window_max_y = y;
        if (cal->window_max_x - cal->window_min_x > MAX_WINDOW_SPREAD ||
            cal->window_max_y - cal->window_min_y > MAX_WINDOW_SPREAD) {
            cal->samples = 0;
            cal->sum_x = cal->sum_y = 0;
            return;
        }
        cal->sum_x += x;
        cal->sum_y += y;
        if (++cal->samples == CENTER_SAMPLES) {
            cal->center_x = (int)(cal->sum_x / cal->samples);
            cal->center_y = (int)(cal->sum_y / cal->samples);
            cal->min_x = cal->max_x = cal->center_x;
            cal->min_y = cal->max_y = cal->center_y;
            cal->ready = true;
        }
        return;
    }
    if (x < cal->min_x) cal->min_x = x;
    if (x > cal->max_x) cal->max_x = x;
    if (y < cal->min_y) cal->min_y = y;
    if (y > cal->max_y) cal->max_y = y;
}

int lab_joystick_normalize(int value, int center, int min, int max)
{
    int delta = value - center;
    if (delta >= -DEADZONE_ADC && delta <= DEADZONE_ADC) return 0;
    int span = delta < 0 ? center - min : max - center;
    if (span < MIN_SPAN) span = MIN_SPAN;
    int magnitude = delta < 0 ? -delta : delta;
    int normalized = (magnitude - DEADZONE_ADC) * 1000 / (span - DEADZONE_ADC);
    if (normalized > 1000) normalized = 1000;
    return delta < 0 ? -normalized : normalized;
}
