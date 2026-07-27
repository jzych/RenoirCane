#pragma once

#include <stdbool.h>

bool calibration_stream_start(void);
void calibration_stream_on_data_ready_from_isr(void);
