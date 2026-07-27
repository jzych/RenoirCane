#include "battery.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_calibrated;

void battery_init(void) { adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1}; adc_oneshot_new_unit(&unit, &s_adc); adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12}; adc_oneshot_config_channel(s_adc, ADC_CHANNEL_7, &channel); adc_cali_line_fitting_config_t cali = {.unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12}; s_calibrated = adc_cali_create_scheme_line_fitting(&cali, &s_cali) == ESP_OK; gpio_set_direction(APP_BAT_CHARGE_GPIO, GPIO_MODE_INPUT); }
float battery_get_voltage(void) { int total = 0; for (int i = 0; i < 16; ++i) { int raw = 0; adc_oneshot_read(s_adc, ADC_CHANNEL_7, &raw); total += raw; } int millivolts = 0; if (s_calibrated) adc_cali_raw_to_voltage(s_cali, total / 16, &millivolts); else millivolts = (total / 16) * 3300 / 4095; return (millivolts * 3.7625f) / 1000.0f; }
bool battery_is_charging(void) { return gpio_get_level(APP_BAT_CHARGE_GPIO) == 0; }
