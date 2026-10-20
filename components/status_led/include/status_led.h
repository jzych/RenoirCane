#pragma once

#include "bluetooth_audio_policy.h"

typedef enum {
    STATUS_LED_OFF,
    STATUS_LED_BOOT,
    STATUS_LED_BT_CONNECTING, /* Also disconnected/searching between retries. */
    STATUS_LED_READY,
    STATUS_LED_PLAYING,
    STATUS_LED_ERROR,
    /* Owned by the task-4 power flow. */
    STATUS_LED_ENTERING_STANDBY,
    STATUS_LED_WAKING,
} status_led_state_t;
void status_led_init(void);
void status_led_set(status_led_state_t state);
void status_led_power_off(void);
/* Shared Bluetooth callback; media transport alone does not mean cue playback. */
void status_led_on_bluetooth_state(bt_audio_state_t state);
