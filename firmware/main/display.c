#include "display.h"
#include "app_ui.h"

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
    app_ui_sleep_warning(fb);
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


esp_err_t lab_display_update(const lab_display_state_t *state)
{
    if (!fb || !state) return ESP_ERR_INVALID_STATE;
    app_ui_render(fb, state);
    return submit_frame();
}
