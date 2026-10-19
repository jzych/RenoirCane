#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct { int16_t x; int16_t y; int16_t z; int64_t timestamp_us; } accel_sample_t;
typedef void (*accelerometer_irq_callback_t)(void);

bool accelerometer_init(accelerometer_irq_callback_t irq_callback);
void accelerometer_prepare_for_sleep(void);
bool accelerometer_set_data_ready_callback(accelerometer_irq_callback_t callback);
bool accelerometer_read_sample(accel_sample_t *sample);
bool accelerometer_read_impact_status(void);
void accelerometer_clear_interrupt(void);
