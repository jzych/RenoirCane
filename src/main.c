#include "audio.h"
#include "esp_log.h"
#include "impact.h"

static const char *TAG = "CANE";

void app_main(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_ACTIVE_AXIS,
        .threshold_raw = APP_IMPACT_THRESHOLD_RAW,
        .refractory_ms = APP_IMPACT_REFRACTORY_MS,
    };
    audio_init();
    impact_init(&config);
    ESP_LOGI(TAG, "impact audio ECU booted; active_axis=%d threshold=%d", config.axis, config.threshold_raw);
}
