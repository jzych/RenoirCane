#pragma once

typedef enum { STATUS_LED_OFF, STATUS_LED_BOOT, STATUS_LED_BT_CONNECTING, STATUS_LED_READY, STATUS_LED_PLAYING, STATUS_LED_ERROR, STATUS_LED_ENTERING_STANDBY, STATUS_LED_WAKING } status_led_state_t;
void status_led_init(void);
void status_led_set(status_led_state_t state);
void status_led_power_off(void);
