#include "touch.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_check.h"

static const char *TAG = "lab_touch";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;

static esp_err_t read_register(uint8_t address, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(device, &address, 1, data, length, 50);
}

esp_err_t lab_touch_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = 7, .scl_io_num = 8,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &bus), TAG, "I2C bus");
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, 0x38, 100), TAG, "address 0x38");
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x38, .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &device), TAG, "I2C device");
    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(read_register(0xaa, &id, 1), TAG, "chip ID");
    ESP_RETURN_ON_FALSE(id == 0x11, ESP_ERR_NOT_SUPPORTED, TAG, "unexpected chip ID 0x%02x", id);
    ESP_LOGI(TAG, "CST826 ID 0x%02x on I2C 0x38", id);
    return ESP_OK;
}

esp_err_t lab_touch_read(lab_touch_state_t *state)
{
    if (!state || !device) return ESP_ERR_INVALID_STATE;
    uint8_t raw[LAB_TOUCH_RAW_BYTES];
    esp_err_t err = read_register(0x02, raw, sizeof(raw));
    if (err != ESP_OK) {
        state->ready = false;
        state->reported = state->valid = 0;
        ++state->errors;
        return err;
    }
    lab_touch_decode(state, raw);
    return ESP_OK;
}
