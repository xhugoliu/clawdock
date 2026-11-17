#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/adc_channel.h"

#define SAMPLE_MS 20
#define REPORT_MS 1000
#define DEBOUNCE_SAMPLES 3

typedef struct {
    const char *name;
    gpio_num_t pin;
    bool stable_pressed;
    bool candidate_pressed;
    unsigned int candidate_samples;
} button_t;

static button_t buttons[] = {
    { .name = "SW1", .pin = GPIO_NUM_50 },
    { .name = "SW2", .pin = GPIO_NUM_49 },
    { .name = "SW3", .pin = GPIO_NUM_5 },
    { .name = "JOY", .pin = GPIO_NUM_4 },
};

static void sample_buttons(void)
{
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        button_t *b = &buttons[i];
        bool pressed = gpio_get_level(b->pin) == 0;
        if (pressed != b->candidate_pressed) {
            b->candidate_pressed = pressed;
            b->candidate_samples = 1;
        } else if (b->candidate_samples < DEBOUNCE_SAMPLES) {
            ++b->candidate_samples;
        }
        if (b->candidate_samples >= DEBOUNCE_SAMPLES && pressed != b->stable_pressed) {
            b->stable_pressed = pressed;
            printf("button t_ms=%lld name=%s state=%s\n",
                   (long long)(esp_timer_get_time() / 1000), b->name,
                   pressed ? "pressed" : "released");
        }
    }
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("\nHachimoDock input lab v0.1; chip revision=%d cores=%d\n",
           chip.revision, chip.cores);
    printf("Buttons are active-low; ADC values are raw and uncalibrated.\n");
    printf("Sample period %d ms; button debounce %d samples; report %d ms.\n",
           SAMPLE_MS, DEBOUNCE_SAMPLES, REPORT_MS);

    uint64_t mask = 0;
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        mask |= 1ULL << buttons[i].pin;
    }
    gpio_config_t pin_config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pin_config));

    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC1_GPIO21_CHANNEL, &channel));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC1_GPIO20_CHANNEL, &channel));

    int min_x = INT32_MAX, max_x = INT32_MIN;
    int min_y = INT32_MAX, max_y = INT32_MIN;
    unsigned int elapsed_ms = 0, errors = 0;
    while (true) {
        sample_buttons();
        int x, y;
        esp_err_t x_result = adc_oneshot_read(adc, ADC1_GPIO21_CHANNEL, &x);
        esp_err_t y_result = adc_oneshot_read(adc, ADC1_GPIO20_CHANNEL, &y);
        if (x_result == ESP_OK && y_result == ESP_OK) {
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        } else {
            ++errors;
        }
        elapsed_ms += SAMPLE_MS;
        if (elapsed_ms >= REPORT_MS) {
            if (min_x == INT32_MAX) {
                printf("adc t_ms=%lld no_valid_samples errors=%u\n",
                       (long long)(esp_timer_get_time() / 1000), errors);
            } else {
                printf("adc t_ms=%lld x_min=%d x_max=%d y_min=%d y_max=%d errors=%u heap_free=%u\n",
                       (long long)(esp_timer_get_time() / 1000), min_x, max_x,
                       min_y, max_y, errors, (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            }
            min_x = min_y = INT32_MAX;
            max_x = max_y = INT32_MIN;
            elapsed_ms = errors = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}
