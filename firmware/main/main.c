#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "display.h"
#include "codex_uart.h"
#include "joystick.h"
#include "screen_idle.h"
#include "touch.h"
#include "touchpad.h"
#include "usb_mouse.h"

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
static struct {
    codex_snapshot_t list;
    codex_detail_t detail;
} display_codex;

/* Native USB uses wheel steps; the serial bridge receives smooth scroll axes separately. */
static void output_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
    static uint8_t previous;
    usb_mouse_report(buttons, x, y, wheel, pan);
    if (buttons != previous || x || y) {
        printf("JT_MOUSE %u %d %d 0 0\n", buttons, x, y);
        previous = buttons;
    }
}

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
    bool awake = true;
    bool warning_shown = false;
    TickType_t last = xTaskGetTickCount();
    while (true) {
        if (xQueuePeek(display_queue, &state, 0) == pdTRUE) {
            status = ESP_OK;
            if (!state.awake && awake) {
                status = lab_display_set_awake(false);
                if (status == ESP_OK) {
                    awake = false;
                    warning_shown = false;
                    printf("display sleep t_ms=%lld\n", (long long)(esp_timer_get_time() / 1000));
                }
            } else if (state.awake) {
                if (state.sleep_warning) {
                    if (!warning_shown) {
                        status = lab_display_sleep_warning();
                        if (status == ESP_OK) {
                            warning_shown = true;
                            printf("display sleep warning t_ms=%lld\n", (long long)(esp_timer_get_time() / 1000));
                        }
                    }
                } else {
                    if (warning_shown) {
                        warning_shown = false;
                        printf("display sleep canceled t_ms=%lld\n", (long long)(esp_timer_get_time() / 1000));
                    }
                    if (state.app.current == CLAWDOCK_CODEX_APP && !state.app.choosing) {
                        state.codex = &display_codex.list;
                        state.codex_detail = state.app.codex_detail ? &display_codex.detail : NULL;
                        state.codex_last_contact_ms = codex_uart_read(&display_codex.list,
                                                                     state.app.codex_detail ? &display_codex.detail : NULL,
                                                                     &state.codex_sources_unavailable);
                    }
                    state.now_ms = esp_timer_get_time() / 1000;
                    if (state.app.current == CLAWDOCK_CLAWD_APP && !state.app.choosing)
                        state.local_minute = codex_uart_local_minute(state.now_ms);
                    status = lab_display_update(&state);
                }
                if (status == ESP_OK && !awake) {
                    status = lab_display_set_awake(true);
                    if (status == ESP_OK) {
                        awake = true;
                        printf("display wake t_ms=%lld\n", (long long)(esp_timer_get_time() / 1000));
                    }
                }
            }
            if (status != ESP_OK) {
                printf("display update failed: %s\n", esp_err_to_name(status));
                vTaskDelete(NULL);
                return;
            }
        }
        bool show_clawd = state.app.current == CLAWDOCK_CLAWD_APP || state.app.current == CLAWDOCK_CODEX_APP;
        bool clawd_active = state.awake && !state.sleep_warning && !state.app.choosing && show_clawd;
        vTaskDelayUntil(&last, pdMS_TO_TICKS(clawd_active ? 50 : 100));
    }
}

static bool sample_buttons(bool diagnostics)
{
    bool activity = false;
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
            activity = true;
            if (diagnostics) printf("button t_ms=%lld name=%s state=%s\n",
                   (long long)(esp_timer_get_time() / 1000), b->name,
                   pressed ? "pressed" : "released");
        }
    }
    return activity;
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("\nClawDock input diagnostics v0.1; chip revision=%d cores=%d\n",
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

    esp_err_t usb_status = usb_mouse_init();
    if (usb_status != ESP_OK) printf("USB mouse init failed: %s\n", esp_err_to_name(usb_status));
    esp_err_t codex_status = codex_uart_init();
    if (codex_status != ESP_OK) printf("Codex serial init failed: %s\n", esp_err_to_name(codex_status));

    app_manager_t apps;
    app_manager_init(&apps);
    printf("app startup current=%s\n", app_name(apps.view.current));
    bool system_awake = true;
    lab_joystick_cal_t joystick = {0};
    touchpad_t touchpad = {0};
    bool scroll_active = false;
    int min_x = INT32_MAX, max_x = INT32_MIN;
    int min_y = INT32_MAX, max_y = INT32_MIN;
    unsigned int errors = 0;
    int64_t report_at = esp_timer_get_time() + REPORT_MS * 1000LL;
    screen_idle_t screen_idle = { .last_event_us = esp_timer_get_time() };
    TickType_t last_sample = xTaskGetTickCount();
    while (true) {
        bool diagnostics = apps.view.current == CLAWDOCK_INPUT_APP && !apps.view.choosing;
        bool button_changed = sample_buttons(diagnostics);
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
            uint8_t diagnostic_max = touch.max_reported;
            esp_err_t touch_err = lab_touch_read(&touch);
            if (!diagnostics) touch.max_reported = diagnostic_max;
            if (touch_err != ESP_OK && touch.errors == 1)
                printf("touch read failed: %s\n", esp_err_to_name(touch_err));
            int64_t now = esp_timer_get_time();
            if (touch_err == ESP_OK && diagnostics && (touch.reported != previous_count || touch.valid != previous_valid ||
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
        {
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
            int64_t now = esp_timer_get_time();
            screen_phase_t phase = screen_idle_update(&screen_idle, now, button_changed, state.joystick_calibrated,
                                                      state.normalized_x, state.normalized_y, &state.touch);
            state.awake = phase != SCREEN_ASLEEP;
            state.sleep_warning = phase == SCREEN_WARNING;
            if (!state.awake && system_awake && apps.view.current == CLAWDOCK_MOUSE_APP) {
                mouse_release(&apps.runtime.mouse);
                output_mouse_report(0, 0, 0, 0, 0);
                touchpad_reset(&touchpad);
                if (scroll_active) printf("JT_SCROLL 0 0\n");
                scroll_active = false;
            }
            if (state.awake && !system_awake) app_manager_wake(&apps);
            system_awake = state.awake;
            if (state.awake) {
                codex_summary_t codex = {0};
                if (apps.view.current == CLAWDOCK_CODEX_APP)
                    codex_uart_summary(&codex, apps.view.codex_detail ? apps.view.codex_id : NULL, now / 1000);
                app_input_t input = {
                    .joystick_ready = state.adc_ok && state.joystick_calibrated,
                    .joystick_x = state.normalized_x,
                    .joystick_y = state.normalized_y,
                    .touch_available = touch_init == ESP_OK,
                    .touch_ready = touch.ready,
                    .touch_count = touch.reported,
                    .touch = &state.touch,
                    .codex = &codex,
                };
                for (unsigned i = 0; i < 4; ++i) input.buttons[i] = state.buttons[i];
                app_view_t previous_app = apps.view;
                app_manager_update(&apps, &input, now / 1000);
                if (previous_app.choosing != apps.view.choosing || previous_app.current != apps.view.current ||
                    previous_app.selected != apps.view.selected) {
                    printf("app t_ms=%lld current=%s selecting=%s selected=%s\n", (long long)(now / 1000),
                           app_name(apps.view.current), apps.view.choosing ? "yes" : "no", app_name(apps.view.selected));
                }
                /* Preserve every button edge, including press/release within the chord window. */
                if (previous_app.current == CLAWDOCK_MOUSE_APP && apps.view.current != CLAWDOCK_MOUSE_APP)
                    output_mouse_report(0, 0, 0, 0, 0);
                if (apps.view.current == CLAWDOCK_MOUSE_APP) {
                    uint8_t button_state;
                    while (mouse_peek(&apps.runtime.mouse, &button_state)) {
                        output_mouse_report(button_state, 0, 0, 0, 0);
                        mouse_pop(&apps.runtime.mouse);
                    }
                    int8_t dx = 0, dy = 0, wheel = 0, pan = 0;
                    bool motion_active = !apps.view.choosing && !apps.input_blocked && input.joystick_ready;
                    mouse_motion(&apps.runtime.mouse, motion_active ? input.joystick_x : 0,
                                 motion_active ? input.joystick_y : 0, now / 1000,
                                 &dx, &dy, &wheel, &pan);
                    output_mouse_report(apps.runtime.mouse.buttons, dx, dy, wheel, pan);
                    bool touch_active = !apps.view.choosing && !apps.input_blocked;
                    if (touch_active) {
                        touchpad_output_t gesture;
                        touchpad_update(&touchpad, &touch, now / 1000, &gesture);
                        if (gesture.dx || gesture.dy || gesture.wheel || gesture.pan)
                            output_mouse_report(apps.runtime.mouse.buttons, gesture.dx, gesture.dy, gesture.wheel, gesture.pan);
                        if (gesture.scroll_x || gesture.scroll_y)
                            printf("JT_TOUCH_SCROLL %d %d\n", gesture.scroll_x, gesture.scroll_y);
                        if (gesture.click && !(apps.runtime.mouse.buttons & gesture.click)) {
                            output_mouse_report(apps.runtime.mouse.buttons | gesture.click, 0, 0, 0, 0);
                            output_mouse_report(apps.runtime.mouse.buttons, 0, 0, 0, 0);
                        }
                    } else touchpad_reset(&touchpad);
                    /* Send calibrated stick positions at the sampling rate for pixel scrolling. */
                    int sx = motion_active && apps.runtime.mouse.scroll_mode ? input.joystick_x : 0;
                    int sy = motion_active && apps.runtime.mouse.scroll_mode ? input.joystick_y : 0;
                    if (sx || sy || scroll_active) printf("JT_SCROLL %d %d\n", sx, sy);
                    scroll_active = sx || sy;
                }
                if (previous_app.current != apps.view.current) {
                    touchpad_reset(&touchpad);
                    /* Touch maximum is diagnostic application state, not a system lifetime statistic. */
                    touch.max_reported = touch.reported;
                    state.touch.max_reported = touch.max_reported;
                    previous_count = previous_valid = 255;
                }
            }
            bool showing_detail = apps.view.current == CLAWDOCK_CODEX_APP && !apps.view.choosing && apps.view.codex_detail;
            codex_uart_request(showing_detail ? apps.view.codex_id : NULL, apps.view.codex_page, now / 1000);
            state.app = apps.view;
            if (display_queue) xQueueOverwrite(display_queue, &state);
        }
        if (esp_timer_get_time() >= report_at) {
            if (diagnostics && min_x == INT32_MAX) {
                printf("adc t_ms=%lld no_valid_samples errors=%u\n",
                       (long long)(esp_timer_get_time() / 1000), errors);
            } else if (diagnostics) {
                printf("adc t_ms=%lld x_min=%d x_max=%d y_min=%d y_max=%d errors=%u heap_free=%u\n",
                       (long long)(esp_timer_get_time() / 1000), min_x, max_x,
                       min_y, max_y, errors, (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            }
            if (diagnostics && joystick.ready && x_result == ESP_OK && y_result == ESP_OK)
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
