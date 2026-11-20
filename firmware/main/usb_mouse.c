#include "usb_mouse.h"
#include <string.h>
#include "tusb.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

typedef struct { uint8_t buttons; int8_t x, y, wheel, pan; } report_t;
static QueueHandle_t reports;
static SemaphoreHandle_t report_lock;
static uint8_t current_buttons;
static bool connected;
static usb_phy_handle_t phy;

static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xcafe, .idProduct = 0x4001, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .bNumConfigurations = 1,
};
static const uint8_t hid_report[] = { TUD_HID_REPORT_DESC_MOUSE() };
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(hid_report), 0x81, 16, 1),
};
uint8_t const *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return configuration; }
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { (void)instance; return hid_report; }
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t desc[32];
    if (index == 0) { desc[0] = (TUSB_DESC_STRING << 8) | 4; desc[1] = 0x0409; return desc; }
    const char *s = index == 1 ? "ClawDock" : index == 2 ? "ClawDock Mouse" : NULL;
    if (!s) return NULL;
    size_t n = strlen(s);
    for (size_t i = 0; i < n; ++i) desc[i + 1] = (uint8_t)s[i];
    desc[0] = (TUSB_DESC_STRING << 8) | (uint16_t)(2 * n + 2);
    return desc;
}
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buffer, uint16_t reqlen)
{ (void)instance; (void)report_id; (void)type; (void)buffer; (void)reqlen; return 0; }
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t const *buffer, uint16_t size)
{ (void)instance; (void)report_id; (void)type; (void)buffer; (void)size; }

static void usb_task(void *arg)
{
    (void)arg;
    report_t pending = {0};
    bool have_pending = false;
    while (true) {
        tud_task_ext(0, false);
        bool ready = tud_mounted();
        xSemaphoreTake(report_lock, portMAX_DELAY);
        if (ready != connected) {
            connected = ready;
            xQueueReset(reports);
            have_pending = false;
            if (ready) {
                report_t initial = { .buttons = current_buttons };
                xQueueSend(reports, &initial, 0);
            }
        }
        if (connected && !have_pending)
            have_pending = xQueueReceive(reports, &pending, 0) == pdTRUE;
        xSemaphoreGive(report_lock);
        if (ready && have_pending && tud_hid_ready() &&
            tud_hid_mouse_report(0, pending.buttons, pending.x, pending.y, pending.wheel, pending.pan))
            have_pending = false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t usb_mouse_init(void)
{
    reports = xQueueCreate(64, sizeof(report_t));
    report_lock = xSemaphoreCreateMutex();
    if (!reports || !report_lock) return ESP_ERR_NO_MEM;
    usb_phy_config_t cfg = { .controller = USB_PHY_CTRL_OTG, .target = USB_PHY_TARGET_INT, .otg_mode = USB_OTG_MODE_DEVICE };
    esp_err_t err = usb_new_phy(&cfg, &phy);
    if (err != ESP_OK) return err;
    if (!tusb_init()) return ESP_FAIL;
    return xTaskCreate(usb_task, "usb_mouse", 4096, NULL, 8, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void usb_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan)
{
    if (!reports || !report_lock) return;
    xSemaphoreTake(report_lock, portMAX_DELAY);
    uint8_t previous = current_buttons;
    current_buttons = buttons;
    if (connected && (previous != buttons || x || y || wheel || pan)) {
        report_t report = { buttons, x, y, wheel, pan };
        if (xQueueSend(reports, &report, 0) != pdTRUE) {
            xQueueReset(reports);
            report_t released = {0};
            xQueueSend(reports, &released, 0);
            report.x = report.y = report.wheel = report.pan = 0;
            xQueueSend(reports, &report, 0);
        }
    }
    xSemaphoreGive(report_lock);
}
