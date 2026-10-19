#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "../../components/accelerometer/accelerometer.c"

static bool disabled[40];
static uint8_t registers[256];
static bool data_ready_handler_added;
static unsigned sample_reads;
esp_err_t i2c_param_config(int port, const i2c_config_t *cfg) { (void)port; (void)cfg; return ESP_OK; }
esp_err_t i2c_driver_install(int p, int m, size_t a, size_t b, int f) { (void)p; (void)m; (void)a; (void)b; (void)f; return ESP_OK; }
esp_err_t i2c_master_write_read_device(int p, int addr, const uint8_t *reg, size_t n, uint8_t *out, size_t len, TickType_t t) {
    (void)p; (void)addr; (void)n; (void)t;
    if (*reg == 0x28) ++sample_reads;
    memcpy(out, &registers[*reg], len); return ESP_OK;
}
esp_err_t i2c_master_write_to_device(int p, int addr, const uint8_t *data, size_t len, TickType_t t) {
    (void)p; (void)addr; (void)t; assert(len == 2);
    if (data[0] == 0x24 && data[1] == 1) {
        assert(data_ready_handler_added);
        assert(sample_reads > 0); /* Drain stale output before enabling INT2. */
    }
    registers[data[0]] = data[1]; return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t *cfg) { (void)cfg; return ESP_OK; }
esp_err_t gpio_install_isr_service(int flags) { (void)flags; return ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t pin, void (*fn)(void *), void *arg) {
    (void)fn; (void)arg;
    if (pin == APP_ACCEL_INT2_GPIO) data_ready_handler_added = true;
    return ESP_OK;
}
esp_err_t gpio_intr_disable(gpio_num_t pin) { disabled[pin] = true; return ESP_OK; }
int64_t esp_timer_get_time(void) { return 0; }

int main(void) {
    registers[0x0f] = 0x44;
    assert(accelerometer_init(NULL));
    assert(registers[0x20] == 0x84);
    assert(accelerometer_set_data_ready_callback(NULL));
    assert(registers[0x24] == 1);
    /* 4500 raw = 2.196 g; tap diagnostics quantize down to 4 * 0.5 g. */
    assert(registers[0x31] == 4);
    accelerometer_prepare_for_sleep();
    assert(registers[0x20] == 0x00);
    assert(registers[0x23] == 0x00 && registers[0x24] == 0x00);
    assert(disabled[14] && disabled[4]);
    puts("PASS: LIS2DW12 ODR power-down and disabled interrupt routing");
    return 0;
}
