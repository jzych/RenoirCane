#pragma once
#include "sdk.h"
#define I2C_NUM_0 0
#define I2C_MODE_MASTER 1
typedef struct {
    int mode, sda_io_num, scl_io_num, sda_pullup_en, scl_pullup_en;
    struct { unsigned clk_speed; } master;
} i2c_config_t;
esp_err_t i2c_param_config(int, const i2c_config_t *);
esp_err_t i2c_driver_install(int, int, size_t, size_t, int);
esp_err_t i2c_master_write_read_device(int, int, const uint8_t *, size_t, uint8_t *, size_t, TickType_t);
esp_err_t i2c_master_write_to_device(int, int, const uint8_t *, size_t, TickType_t);
