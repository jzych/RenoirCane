#pragma once
#include <stdbool.h>
void battery_init(void);
float battery_get_voltage(void);
bool battery_is_charging(void);
