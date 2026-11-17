#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "display.h"
#include "joystick.h"
#include "touch.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
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

static QueueHandle_t display_queue;

static void display_task(void *unused)
{
    (void)unused;
    esp_err_t status = lab_display_init();
    if (status != ESP_OK) {
        printf("display init failed: %s; serial input test remains available\n", esp_err_to_name(status));
        vTaskDelete(NULL);
        return;
    }
    lab_display_state_t state = {0};
    TickType_t last = xTaskGetTickCount();
    while (true) {
        if (xQueuePeek(display_queue, &state, 0) == pdTRUE) {
            status = lab_display_update(&state);
            if (status != ESP_OK) {
                printf("display update failed: %s\n", esp_err_to_name(status));
                vTaskDelete(NULL);
                return;
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
    }
}

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
    printf("\nJintai input diagnostics v0.1; chip revision=%d cores=%d\n",
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

    lab_touch_state_t touch = {0};
    esp_err_t touch_init = lab_touch_init();
    if (touch_init != ESP_OK) printf("touch init failed: %s\n", esp_err_to_name(touch_init));
    uint8_t previous_count = 255, previous_valid = 255;
    int64_t next_touch_log = 0;
    display_queue = xQueueCreate(1, sizeof(lab_display_state_t));
    if (!display_queue || xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 1, NULL, 1) != pdPASS) {
        printf("display task unavailable; serial input test remains available\n");
        display_queue = NULL;
    }

    lab_joystick_cal_t joystick = {0};
    int min_x = INT32_MAX, max_x = INT32_MIN;
    int min_y = INT32_MAX, max_y = INT32_MIN;
    unsigned int errors = 0;
    int64_t report_at = esp_timer_get_time() + REPORT_MS * 1000LL;
    TickType_t last_sample = xTaskGetTickCount();
    while (true) {
        sample_buttons();
        int x, y;
        esp_err_t x_result = adc_oneshot_read(adc, ADC1_GPIO21_CHANNEL, &x);
        esp_err_t y_result = adc_oneshot_read(adc, ADC1_GPIO20_CHANNEL, &y);
        if (x_result == ESP_OK && y_result == ESP_OK) {
            bool was_ready = joystick.ready;
            lab_joystick_update(&joystick, x, y);
            if (!was_ready && joystick.ready)
                printf("joystick center calibrated x=%d y=%d\n", joystick.center_x, joystick.center_y);
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        } else {
            ++errors;
        }
        if (touch_init == ESP_OK) {
            esp_err_t touch_err = lab_touch_read(&touch);
            if (touch_err != ESP_OK && touch.errors == 1)
                printf("touch read failed: %s\n", esp_err_to_name(touch_err));
            int64_t now = esp_timer_get_time();
            if (touch_err == ESP_OK && (touch.reported != previous_count || touch.valid != previous_valid ||
                (touch.reported && now >= next_touch_log))) {
                printf("touch t_ms=%lld reported=%u valid=%u max=%u errors=%lu raw=",
                       (long long)(now / 1000), touch.reported, touch.valid,
                       touch.max_reported, (unsigned long)touch.errors);
                for (unsigned i = 0; i < LAB_TOUCH_RAW_BYTES; ++i) printf("%02x", touch.raw[i]);
                for (unsigned i = 0; i < touch.valid; ++i)
                    printf(" point%u=%u,%u,id%u", i + 1, touch.points[i].x,
                           touch.points[i].y, touch.points[i].id);
                printf("\n");
                previous_count = touch.reported;
                previous_valid = touch.valid;
                next_touch_log = now + 1000000;
            }
        }
        if (display_queue) {
            lab_display_state_t state = { .touch = touch, .adc_ok = x_result == ESP_OK && y_result == ESP_OK };
            for (size_t i = 0; i < 4; ++i) state.buttons[i] = buttons[i].stable_pressed;
            if (state.adc_ok) {
                state.x = x; state.y = y;
                state.joystick_calibrated = joystick.ready;
                if (joystick.ready) {
                    state.normalized_x = lab_joystick_normalize(x, joystick.center_x, joystick.min_x, joystick.max_x);
                    state.normalized_y = lab_joystick_normalize(y, joystick.center_y, joystick.min_y, joystick.max_y);
                }
            }
            xQueueOverwrite(display_queue, &state);
        }
        if (esp_timer_get_time() >= report_at) {
            if (min_x == INT32_MAX) {
                printf("adc t_ms=%lld no_valid_samples errors=%u\n",
                       (long long)(esp_timer_get_time() / 1000), errors);
            } else {
                printf("adc t_ms=%lld x_min=%d x_max=%d y_min=%d y_max=%d errors=%u heap_free=%u\n",
                       (long long)(esp_timer_get_time() / 1000), min_x, max_x,
                       min_y, max_y, errors, (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            }
            if (joystick.ready && x_result == ESP_OK && y_result == ESP_OK)
                printf("joystick center=%d,%d observed_x=%d,%d observed_y=%d,%d normalized=%d,%d\n",
                       joystick.center_x, joystick.center_y, joystick.min_x, joystick.max_x,
                       joystick.min_y, joystick.max_y,
                       lab_joystick_normalize(x, joystick.center_x, joystick.min_x, joystick.max_x),
                       lab_joystick_normalize(y, joystick.center_y, joystick.min_y, joystick.max_y));
            min_x = min_y = INT32_MAX;
            max_x = max_y = INT32_MIN;
            errors = 0;
            report_at = esp_timer_get_time() + REPORT_MS * 1000LL;
        }
        vTaskDelayUntil(&last_sample, pdMS_TO_TICKS(SAMPLE_MS));
    }
}
