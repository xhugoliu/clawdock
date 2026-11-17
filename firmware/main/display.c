#include "display.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_st7701_interface.h"
#include "esp_ldo_regulator.h"
#include "hal/mipi_dsi_host_ll.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "panel_commands.h"

#define WIDTH 640
#define HEIGHT 480
#define PANEL_WIDTH 480
#define PANEL_HEIGHT 640

static const char *TAG = "lab_display";
static uint16_t *fb;
static uint16_t *framebuffers[2];
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t refresh_done;
static unsigned int draw_index = 1;

static bool IRAM_ATTR on_refresh(esp_lcd_panel_handle_t handle,
                                esp_lcd_dpi_panel_event_data_t *event, void *context)
{
    (void)handle;
    (void)event;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &task_woken);
    return task_woken == pdTRUE;
}

static uint16_t rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

/* One landscape pixel maps to the panel's clockwise quarter-turn. */
static void pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x < WIDTH && (unsigned)y < HEIGHT)
        fb[(size_t)x * PANEL_WIDTH + (PANEL_WIDTH - 1 - y)] = color;
}

static void rect(int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            pixel(xx, yy, color);
}

/* 5x7 glyphs used by the diagnostic screen and sleep warning. */
static const uint8_t font[][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}, {0,0,0,31,0,0,0},
    {0,0,31,2,4,8,31}, {0,0,0,0,0,6,6}
};

static void text_at(int x, int y, const char *s, int scale, uint16_t color)
{
    for (; *s; ++s, x += 6 * scale) {
        int index = *s >= '0' && *s <= '9' ? *s - '0' :
                    *s >= 'A' && *s <= 'Z' ? 10 + *s - 'A' :
                    *s == '-' ? 36 : *s == 'z' ? 37 : *s == '.' ? 38 : -1;
        if (index < 0) continue;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (font[index][row] & (1 << (4 - col)))
                    rect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

static esp_err_t submit_frame(void)
{
    /* Wait two refreshes before reusing the previous front buffer. */
    while (xSemaphoreTake(refresh_done, 0) == pdTRUE) {}
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, 0, 0, PANEL_WIDTH, PANEL_HEIGHT, fb),
                        TAG, "submit framebuffer");
    for (int i = 0; i < 2; ++i) {
        ESP_RETURN_ON_FALSE(xSemaphoreTake(refresh_done, pdMS_TO_TICKS(250)) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "display refresh timeout");
    }
    draw_index ^= 1;
    fb = framebuffers[draw_index];
    return ESP_OK;
}

esp_err_t lab_display_sleep_warning(void)
{
    if (!fb) return ESP_ERR_INVALID_STATE;
    rect(0, 0, WIDTH, HEIGHT, rgb(0, 0, 0));
    /* Six 5x7 glyphs with one column of spacing, centered on the screen. */
    text_at((WIDTH - 35 * 10) / 2, (HEIGHT - 7 * 10) / 2, "Zzz...", 10, rgb(255, 255, 255));
    return submit_frame();
}

esp_err_t lab_display_init(void)
{
    static esp_ldo_channel_handle_t power;
    static esp_lcd_dsi_bus_handle_t bus;
    static esp_lcd_panel_io_handle_t io;
    gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << GPIO_NUM_26, .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight gpio");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_26, 0), TAG, "backlight off");
    esp_ldo_channel_config_t ldo = { .chan_id = 3, .voltage_mv = 2500 };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo, &power), TAG, "DSI power");
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0, .num_data_lanes = 1, .phy_clk_src = 0, .lane_bit_rate_mbps = 800,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &bus), TAG, "DSI bus");
    esp_lcd_dbi_io_config_t io_config = {
        .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &io_config, &io), TAG, "DBI");
    /* WLK2802's command return path is not wired. */
    mipi_dsi_host_ll_enable_cmd_ack(MIPI_DSI_LL_GET_HOST(0), false);
    esp_lcd_dpi_panel_config_t dpi = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 24,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = PANEL_WIDTH, .v_size = PANEL_HEIGHT,
            .hsync_back_porch = 20, .hsync_pulse_width = 4, .hsync_front_porch = 10,
            .vsync_back_porch = 14, .vsync_pulse_width = 4, .vsync_front_porch = 8,
        },
    };
    st7701_vendor_config_t vendor = {
        .init_cmds = vendor_specific_init_default,
        .init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(vendor_specific_init_default[0]),
        .flags = { .use_mipi_interface = 1, .skip_mipi_id_read = 1 },
        .mipi_config = { .dsi_bus = bus, .dpi_config = &dpi },
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 16, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = GPIO_NUM_27, .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701_mipi(io, &panel_config, &panel), TAG, "panel create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 2,
                        (void **)&framebuffers[0], (void **)&framebuffers[1]), TAG, "framebuffers");
    refresh_done = xSemaphoreCreateCounting(4, 0);
    ESP_RETURN_ON_FALSE(refresh_done, ESP_ERR_NO_MEM, TAG, "refresh semaphore");
    esp_lcd_dpi_panel_event_callbacks_t callbacks = { .on_refresh_done = on_refresh };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, refresh_done),
                        TAG, "refresh callback");
    fb = framebuffers[draw_index];
    lab_display_state_t initial = {0};
    ESP_RETURN_ON_ERROR(lab_display_update(&initial), TAG, "first frame");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_26, 1), TAG, "backlight on");
    ESP_LOGI(TAG, "display initialized 640x480 landscape");
    return ESP_OK;
}

esp_err_t lab_display_set_awake(bool awake)
{
    if (!awake) {
        ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_26, 0), TAG, "backlight off");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, false), TAG, "panel off");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_sleep(panel, true), TAG, "panel sleep");
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_sleep(panel, false), TAG, "panel wake");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "panel on");
        ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_26, 1), TAG, "backlight on");
    }
    return ESP_OK;
}

static void circle(int cx, int cy, int radius, uint16_t color)
{
    int outer = radius * radius;
    int inner = (radius - 4) * (radius - 4);
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            int distance = x * x + y * y;
            if (distance <= outer && distance >= inner) pixel(cx + x, cy + y, color);
        }
}

esp_err_t lab_display_update(const lab_display_state_t *state)
{
    if (!fb || !state) return ESP_ERR_INVALID_STATE;
    const bool *buttons = state->buttons;
    const lab_touch_state_t *touch = &state->touch;
    bool adc_ok = state->adc_ok;
    const uint16_t bg = rgb(11, 19, 35), card = rgb(22, 35, 56);
    const uint16_t white = rgb(238, 245, 255), muted = rgb(137, 160, 186);
    const uint16_t accent = rgb(54, 222, 174), blue = rgb(77, 153, 255);
    rect(0, 0, WIDTH, HEIGHT, bg);
    rect(0, 0, WIDTH, 6, accent);
    text_at(26, 24, "JINTAI", 3, white);
    text_at(27, 55, "INPUT DIAGNOSTICS", 2, muted);
    text_at(27, 96, "BUTTONS", 2, muted);
    const char *labels[] = {"SW1", "SW2", "SW3", "JOY"};
    for (int i = 0; i < 4; ++i) {
        int bx = 26 + i * 150;
        rect(bx, 120, 134, 78, buttons[i] ? accent : card);
        text_at(bx + 12, 132, labels[i], 3, buttons[i] ? bg : white);
        text_at(bx + 12, 170, buttons[i] ? "PRESSED" : "READY", 2, buttons[i] ? bg : muted);
    }
    rect(26, 222, 588, 200, card);
    text_at(46, 242, "JOYSTICK", 2, white);
    char value[16];
    text_at(46, 284, "X", 3, blue);
    snprintf(value, sizeof(value), "%d", state->normalized_x);
    text_at(80, 284, adc_ok && state->joystick_calibrated ? value : "WAIT", 3, white);
    text_at(46, 338, "Y", 3, accent);
    snprintf(value, sizeof(value), "%d", state->normalized_y);
    text_at(80, 338, adc_ok && state->joystick_calibrated ? value : "WAIT", 3, white);
    /* Equal pixel scales around the calibrated center; values include the deadzone. */
    rect(360, 253, 212, 142, bg);
    circle(465, 324, 60, muted);
    rect(464, 265, 2, 118, muted);
    rect(406, 323, 118, 2, muted);
    if (adc_ok && state->joystick_calibrated) {
        int px = 465 + state->normalized_x * 55 / 1000;
        int py = 324 - state->normalized_y * 55 / 1000;
        rect(px - 5, py - 5, 10, 10, accent);
    }
    text_at(380, 401, state->joystick_calibrated ? "NORMALIZED" : "CENTER WAIT", 1, muted);
    text_at(27, 445, "XY NORMALIZED  DEADZONE 60", 2, muted);
    char touch_text[32];
    if (touch->ready) {
        snprintf(touch_text, sizeof(touch_text), "TOUCH %u MAX %u", touch->reported, touch->max_reported);
    } else {
        snprintf(touch_text, sizeof(touch_text), "TOUCH ERROR");
    }
    text_at(390, 26, touch_text, 2, white);
    snprintf(touch_text, sizeof(touch_text), "VALID %u", touch->valid);
    text_at(390, 52, touch_text, 2, muted);
    if (touch->reported > LAB_TOUCH_POINTS) text_at(500, 52, "OVER TWO", 1, white);
    const uint16_t touch_colors[2] = {rgb(54, 222, 174), rgb(255, 172, 76)};
    for (unsigned i = 0; i < touch->valid; ++i) {
        circle(touch->points[i].x, touch->points[i].y, 22, touch_colors[i]);
        rect((int)touch->points[i].x - 2, (int)touch->points[i].y - 2, 5, 5, touch_colors[i]);
    }
    return submit_frame();
}
