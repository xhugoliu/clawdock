#pragma once

#include "codex_monitor.h"
#include "esp_err.h"

esp_err_t codex_uart_init(void);
void codex_uart_request(const uint8_t *id, unsigned page, int64_t now_ms);
void codex_uart_summary(codex_summary_t *out, const uint8_t *detail_id, int64_t now_ms);
int64_t codex_uart_read(codex_snapshot_t *list, codex_detail_t *detail, bool *unavailable);
int codex_uart_local_minute(int64_t now_ms);
