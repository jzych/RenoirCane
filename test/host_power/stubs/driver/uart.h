#pragma once
#include "sdk.h"
#define UART_NUM_0 0
bool uart_is_driver_installed(int port);
esp_err_t uart_driver_install(int port, int rx, int tx, int queue_size, void *queue, int flags);
esp_err_t uart_set_baudrate(int port, uint32_t baud);
esp_err_t uart_wait_tx_done(int port, TickType_t wait);
int uart_read_bytes(int port, void *buffer, uint32_t length, TickType_t wait);
int uart_write_bytes(int port, const void *buffer, size_t length);
