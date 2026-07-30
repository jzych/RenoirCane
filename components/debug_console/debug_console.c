#include "debug_console.h"

#include <stdio.h>
#include <string.h>
#include "accelerometer.h"
#include "app_config.h"
#include "audio.h"
#include "battery.h"
#include "bluetooth_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void print_status(void) {
    printf("battery_voltage=%.3f\n", battery_get_voltage());
    printf("battery_charging=%d\n", battery_is_charging());
    printf("audio_playing=%d\n", audio_is_active());
    printf("accel_address=0x%02x\n", APP_ACCEL_I2C_ADDR);
    printf("accel_odr_hz=%d\n", APP_ACCEL_ODR_HZ);
    printf("accel_range_g=%d\n", APP_ACCEL_RANGE_G);
    printf("impact_axis=y\nimpact_refractory_ms=%u\n", APP_IMPACT_REFRACTORY_MS);
    printf("bt_state=%d\n", (int)bluetooth_audio_get_state());
    printf("bt_target=78:66:F3:3D:7E:4D\n");
}

static const char *playback_result_name(bt_audio_play_result_t result) {
    switch (result) {
        case BT_AUDIO_PLAY_STARTED: return "started";
        case BT_AUDIO_PLAY_DISCONNECTED: return "disconnected";
        case BT_AUDIO_PLAY_BUSY: return "busy";
        case BT_AUDIO_PLAY_ERROR: return "error";
    }
    return "error";
}

static void console_task(void *arg) {
    (void)arg;
    char line[80];
    printf("cane> type help\n");
    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "help") == 0) printf("help\nstatus\naudio play\ncapture dump\nbt status\nbt reconnect\n");
        else if (strcmp(line, "status") == 0) print_status();
        else if (strcmp(line, "audio play") == 0) printf("audio_play=%s\n", playback_result_name(bluetooth_audio_start_impact()));
        else if (strcmp(line, "capture dump") == 0) printf("capture=not-yet-implemented\n");
        else if (strcmp(line, "bt status") == 0) print_status();
        else if (strcmp(line, "bt reconnect") == 0) {
            bluetooth_audio_request_reconnect();
            printf("bt_reconnect=requested\n");
        }
        else if (line[0] != '\0') printf("unknown_command=%s\n", line);
    }
}

void debug_console_init(void) { xTaskCreate(console_task, "console", 4096, NULL, 4, NULL); }
