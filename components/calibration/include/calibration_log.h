#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Called only by calibration mode, before starting Bluetooth or the UART stream.
 * Capture remains active until reset, including when later initialization fails. */
bool calibration_log_init(void);
/* Sole consumer is the transport task. Producers never wait for this consumer. */
size_t calibration_log_read(uint8_t *output, size_t capacity);
uint32_t calibration_log_take_dropped_bytes(void);
/* Add transport losses or restore an unsent notice; saturates at UINT32_MAX. */
void calibration_log_record_dropped_bytes(size_t count);
