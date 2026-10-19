#include "accelerometer.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"

#define LIS2DW12_WHO_AM_I 0x0F
#define LIS2DW12_WHO_AM_I_VALUE 0x44
#define LIS2DW12_CTRL1 0x20
#define LIS2DW12_CTRL2 0x21
#define LIS2DW12_CTRL3 0x22
#define LIS2DW12_CTRL4_INT1_PAD_CTRL 0x23
#define LIS2DW12_CTRL5_INT2_PAD_CTRL 0x24
#define LIS2DW12_CTRL6 0x25
#define LIS2DW12_TAP_THS_X 0x30
#define LIS2DW12_TAP_THS_Y 0x31
#define LIS2DW12_TAP_THS_Z 0x32
#define LIS2DW12_INT_DUR 0x33
#define LIS2DW12_TAP_SRC 0x39
#define LIS2DW12_ALL_INT_SRC 0x3B
#define LIS2DW12_CTRL7 0x3F
#define LIS2DW12_OUT_X_L 0x28

static const char *TAG = "ACCEL";
static accelerometer_irq_callback_t s_callback;
static accelerometer_irq_callback_t s_data_ready_callback;
static bool s_isr_service_installed;

static bool read_register(uint8_t reg, uint8_t *data, size_t length) {
    return i2c_master_write_read_device(APP_I2C_PORT, APP_ACCEL_I2C_ADDR, &reg, 1, data, length, pdMS_TO_TICKS(50)) == ESP_OK;
}

static bool write_register(uint8_t reg, uint8_t value) {
    uint8_t bytes[] = {reg, value};
    return i2c_master_write_to_device(APP_I2C_PORT, APP_ACCEL_I2C_ADDR, bytes, sizeof(bytes), pdMS_TO_TICKS(50)) == ESP_OK;
}

static void IRAM_ATTR accel_isr(void *arg) { (void)arg; if (s_callback != NULL) s_callback(); }
static void IRAM_ATTR data_ready_isr(void *arg) { (void)arg; if (s_data_ready_callback != NULL) s_data_ready_callback(); }

static bool install_isr_service(void) {
    if (s_isr_service_installed) return true;
    esp_err_t result = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return false;
    s_isr_service_installed = true;
    return true;
}

static bool configure_tap_diagnostics(void) {
    /* AN5038: TAP_THS steps are FS/32. Round down so this coarse diagnostic
       threshold does not sit above the exact software threshold. */
    uint32_t threshold = (uint32_t)(APP_IMPACT_THRESHOLD_RAW * APP_ACCEL_SENSITIVITY_MG_LSB
                                   * 32.0f / (APP_ACCEL_RANGE_G * 1000.0f));
    if (threshold < 1U) threshold = 1U;
    if (threshold > 31U) threshold = 31U;
    uint8_t axis_enable = (uint8_t)(0x80U >> APP_IMPACT_ACTIVE_AXIS);
    return write_register(LIS2DW12_TAP_THS_X, (uint8_t)threshold) &&
           write_register(LIS2DW12_TAP_THS_Y, (uint8_t)threshold) &&
           write_register(LIS2DW12_TAP_THS_Z, (uint8_t)threshold | axis_enable) &&
           write_register(LIS2DW12_INT_DUR, 0x06);
}

bool accelerometer_init(accelerometer_irq_callback_t irq_callback) {
    i2c_config_t i2c = {.mode = I2C_MODE_MASTER, .sda_io_num = APP_I2C_SDA_GPIO, .scl_io_num = APP_I2C_SCL_GPIO, .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE, .master.clk_speed = APP_I2C_FREQ_HZ};
    if (i2c_param_config(APP_I2C_PORT, &i2c) != ESP_OK || i2c_driver_install(APP_I2C_PORT, i2c.mode, 0, 0, 0) != ESP_OK) return false;
    uint8_t identity = 0;
    if (!read_register(LIS2DW12_WHO_AM_I, &identity, 1) || identity != LIS2DW12_WHO_AM_I_VALUE) { ESP_LOGE(TAG, "WHO_AM_I=0x%02x", identity); return false; }
    if (!write_register(LIS2DW12_CTRL1, 0x84) || !write_register(LIS2DW12_CTRL2, 0x0C) || !write_register(LIS2DW12_CTRL3, 0x10) || !write_register(LIS2DW12_CTRL6, 0x30) ||
        !configure_tap_diagnostics() ||
        !write_register(LIS2DW12_CTRL4_INT1_PAD_CTRL, irq_callback != NULL ? 0x40 : 0x00) || !write_register(LIS2DW12_CTRL7, 0x20)) return false;
    s_callback = irq_callback;
    gpio_config_t gpio = {.pin_bit_mask = 1ULL << APP_ACCEL_INT1_GPIO, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_POSEDGE};
    if (gpio_config(&gpio) != ESP_OK || !install_isr_service() || gpio_isr_handler_add(APP_ACCEL_INT1_GPIO, accel_isr, NULL) != ESP_OK) return false;
    ESP_LOGI(TAG, "LIS2DW12 detected at 0x%02x; INT1=GPIO%d INT2=GPIO%d", APP_ACCEL_I2C_ADDR, APP_ACCEL_INT1_GPIO, APP_ACCEL_INT2_GPIO);
    return true;
}

bool accelerometer_set_data_ready_callback(accelerometer_irq_callback_t callback) {
    if (!write_register(LIS2DW12_CTRL5_INT2_PAD_CTRL, 0x00)) return false;
    s_data_ready_callback = callback;
    gpio_config_t gpio = {.pin_bit_mask = 1ULL << APP_ACCEL_INT2_GPIO, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_POSEDGE};
    if (gpio_config(&gpio) != ESP_OK || !install_isr_service() || gpio_isr_handler_add(APP_ACCEL_INT2_GPIO, data_ready_isr, NULL) != ESP_OK) return false;
    /* Clear a stale data-ready level before routing it. The ISR must already
       be installed when routing is enabled, including at first startup. */
    accel_sample_t initial_sample;
    return accelerometer_read_sample(&initial_sample) && write_register(LIS2DW12_CTRL5_INT2_PAD_CTRL, 0x01);
}

void accelerometer_prepare_for_sleep(void) {
    (void)gpio_intr_disable(APP_ACCEL_INT1_GPIO);
    (void)gpio_intr_disable(APP_ACCEL_INT2_GPIO);
    bool int1_disabled = write_register(LIS2DW12_CTRL4_INT1_PAD_CTRL, 0x00);
    bool int2_disabled = write_register(LIS2DW12_CTRL5_INT2_PAD_CTRL, 0x00);
    /* ODR=0000 selects power-down regardless of the previous operating mode. */
    bool powered_down = write_register(LIS2DW12_CTRL1, 0x00);
    if (!int1_disabled || !int2_disabled || !powered_down) {
        ESP_LOGW(TAG, "LIS2DW12 standby preparation failed");
    }
}

bool accelerometer_read_sample(accel_sample_t *sample) {
    uint8_t raw[6];
    if (sample == NULL || !read_register(LIS2DW12_OUT_X_L, raw, sizeof(raw))) return false;
    sample->x = (int16_t)((raw[1] << 8) | raw[0]); sample->y = (int16_t)((raw[3] << 8) | raw[2]); sample->z = (int16_t)((raw[5] << 8) | raw[4]); sample->timestamp_us = esp_timer_get_time(); return true;
}

bool accelerometer_read_impact_status(void) { uint8_t source; return read_register(LIS2DW12_ALL_INT_SRC, &source, 1) && (source & 0x04) != 0; }
void accelerometer_clear_interrupt(void) { uint8_t source; (void)read_register(LIS2DW12_TAP_SRC, &source, 1); (void)read_register(LIS2DW12_ALL_INT_SRC, &source, 1); }
