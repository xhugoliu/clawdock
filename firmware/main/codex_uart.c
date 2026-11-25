#include "codex_uart.h"
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static SemaphoreHandle_t lock;
static codex_snapshot_t latest;
static codex_detail_t latest_detail;
static codex_decoder_t decoder;
static int64_t last_contact_ms;
static bool sources_unavailable;
static int64_t clock_received_ms;
static uint32_t clock_day_ms;
static union {
    codex_snapshot_t list;
    codex_detail_t detail;
    uint32_t clock_ms;
} incoming;

static void receive_task(void *unused)
{
    (void)unused;
    uint8_t bytes[512];
    int64_t last_byte_ms = 0;
    int previous_active = -1, previous_waiting = -1, previous_count = -1, previous_available = -1;
    int previous_page = -1, previous_pages = -1;
    while (true) {
        int count = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, bytes, sizeof(bytes), pdMS_TO_TICKS(40));
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (decoder.used && now_ms - last_byte_ms > 250) decoder.used = 0;
        if (count > 0) last_byte_ms = now_ms;
        for (int i = 0; i < count; ++i) {
            codex_frame_t frame = codex_decoder_byte(&decoder, bytes[i], &incoming.list, &incoming.detail,
                                                      &incoming.clock_ms);
            if (frame == CODEX_FRAME_NONE) continue;
            if (frame == CODEX_FRAME_CLOCK) {
                xSemaphoreTake(lock, portMAX_DELAY);
                bool first = !clock_received_ms;
                clock_day_ms = incoming.clock_ms;
                clock_received_ms = now_ms;
                xSemaphoreGive(lock);
                if (first) {
                    unsigned minute = incoming.clock_ms / 60000;
                    printf("JT_TIME SYNC %02u:%02u\n", minute / 60, minute % 60);
                }
                continue;
            }
            if (frame == CODEX_FRAME_DETAIL) {
                incoming.detail.received_ms = now_ms;
                xSemaphoreTake(lock, portMAX_DELAY);
                last_contact_ms = now_ms;
                latest_detail = incoming.detail;
                xSemaphoreGive(lock);
                if (previous_page != incoming.detail.page || previous_pages != incoming.detail.pages) {
                    printf("JT_CODEX DETAIL page=%u pages=%u\n", incoming.detail.page, incoming.detail.pages);
                    previous_page = incoming.detail.page;
                    previous_pages = incoming.detail.pages;
                }
                continue;
            }
            incoming.list.received_ms = now_ms;
            xSemaphoreTake(lock, portMAX_DELAY);
            last_contact_ms = now_ms;
            sources_unavailable = !incoming.list.available;
            if (incoming.list.available) latest = incoming.list;
            xSemaphoreGive(lock);

            int active = 0, waiting = 0;
            for (unsigned j = 0; j < incoming.list.count; ++j) {
                uint8_t state = incoming.list.rows[j].session.state;
                active += state == CODEX_RUNNING;
                waiting += state == CODEX_WAITING_FOR_ANSWER;
            }
            if (previous_count != (int)incoming.list.count || previous_active != active ||
                previous_available != incoming.list.available || previous_waiting != waiting) {
                printf("JT_CODEX SYNC rows=%u active=%d available=%d waiting=%d\n",
                       incoming.list.count, active, incoming.list.available, waiting);
                previous_count = (int)incoming.list.count;
                previous_active = active;
                previous_waiting = waiting;
                previous_available = incoming.list.available;
            }
        }
    }
}

esp_err_t codex_uart_init(void)
{
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    esp_err_t result = uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 4096, 0, 0, NULL, 0);
    if (result == ESP_OK) {
        if (xTaskCreate(receive_task, "codex_rx", 3072, NULL, 4, NULL) == pdPASS) return ESP_OK;
        uart_driver_delete(CONFIG_ESP_CONSOLE_UART_NUM);
        result = ESP_ERR_NO_MEM;
    }
    vSemaphoreDelete(lock);
    lock = NULL;
    return result;
}

void codex_uart_request(const uint8_t *id, unsigned page, int64_t now_ms)
{
    static int64_t next_request_ms;
    static int64_t next_list_ms;
    static int64_t next_clock_request_ms;
    static bool was_detail;
    static unsigned last_page;
    static uint8_t last_id[CODEX_ID_BYTES];
    if (!lock) return;
    if (now_ms >= next_clock_request_ms) {
        printf("JT_TIME 1\n");
        next_clock_request_ms = now_ms + 5000;
    }
    bool detail = id != NULL;
    bool changed = detail != was_detail || (detail && (page != last_page || memcmp(last_id, id, CODEX_ID_BYTES) != 0));
    if (!changed && now_ms < next_request_ms) return;
    was_detail = detail;
    last_page = page;
    next_request_ms = now_ms + 2000;
    // Use a periodic request slot for the list. A page change always requests detail immediately.
    if (!detail || (!changed && now_ms >= next_list_ms)) {
        next_list_ms = now_ms + 4000;
        printf("JT_CODEX 4\n");
        return;
    }
    memcpy(last_id, id, CODEX_ID_BYTES);
    static const char digits[] = "0123456789abcdef";
    char uuid[37];
    unsigned index = 0;
    for (unsigned i = 0; i < CODEX_ID_BYTES; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) uuid[index++] = '-';
        uuid[index++] = digits[id[i] >> 4];
        uuid[index++] = digits[id[i] & 0x0f];
    }
    uuid[index] = '\0';
    printf("JT_CODEX 4 %s %u\n", uuid, page);
}

void codex_uart_summary(codex_summary_t *out, const uint8_t *detail_id, int64_t now_ms)
{
    *out = (codex_summary_t){0};
    if (!lock) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    out->received_ms = latest.received_ms;
    out->sources_unavailable = sources_unavailable;
    out->count = latest.count;
    for (unsigned i = 0; i < out->count; ++i) {
        memcpy(out->ids[i], latest.rows[i].id, CODEX_ID_BYTES);
        out->states[i] = latest.rows[i].session.state;
    }
    if (detail_id && latest_detail.received_ms && now_ms - latest_detail.received_ms <= CODEX_TIMEOUT_MS &&
        memcmp(latest_detail.id, detail_id, CODEX_ID_BYTES) == 0) {
        out->detail_pages = latest_detail.pages;
    }
    xSemaphoreGive(lock);
}

int64_t codex_uart_read(codex_snapshot_t *list, codex_detail_t *detail, bool *unavailable)
{
    if (!lock) return 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (list) *list = latest;
    if (detail) *detail = latest_detail;
    if (unavailable) *unavailable = sources_unavailable;
    int64_t last_contact = last_contact_ms;
    xSemaphoreGive(lock);
    return last_contact;
}

int codex_uart_local_minute(int64_t now_ms)
{
    if (!lock) return -1;
    xSemaphoreTake(lock, portMAX_DELAY);
    int64_t synced_at = clock_received_ms;
    uint32_t day_ms = clock_day_ms;
    xSemaphoreGive(lock);
    if (!synced_at) return -1;
    int64_t elapsed = now_ms > synced_at ? now_ms - synced_at : 0;
    return (day_ms + elapsed) % CLAWDOCK_DAY_MS / 60000;
}
