#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "../../components/calibration/calibration_mode.c"

static char order[64];
static size_t steps;
static bool log_ok = true, stream_ok = true, accel_ok = true;
static status_led_state_t led;
static void step(char c) { order[steps++] = c; }
bool calibration_log_init(void) { step('L'); return log_ok; }
void status_led_init(void) { step('S'); }
void status_led_set(status_led_state_t state) { led = state; }
void audio_init(void) { step('A'); }
void impact_init(const impact_config_t *config) { assert(config); step('I'); }
bool calibration_stream_start(void) { step('T'); return stream_ok; }
bool bluetooth_audio_init(bt_audio_state_callback_t callback) {
    assert(callback); step('B'); return true;
}
void bluetooth_audio_start(void) { step('b'); }
bool accelerometer_init(accelerometer_irq_callback_t callback) {
    assert(callback == calibration_stream_on_impact_from_isr); step('X'); return accel_ok;
}
bool accelerometer_set_data_ready_callback(accelerometer_irq_callback_t callback) {
    assert(callback == calibration_stream_on_data_ready_from_isr); step('D'); return true;
}
bool accelerometer_read_sample(accel_sample_t *sample) { (void)sample; step('R'); return true; }
void calibration_stream_on_impact_from_isr(void) {}
void calibration_stream_on_data_ready_from_isr(void) {}
static void run(void) { steps = 0; memset(order, 0, sizeof(order)); calibration_mode_start(); }
int main(void) {
    run();
    assert(strcmp(order, "SLAITBbXDR") == 0);
    log_ok = false; run();
    assert(strcmp(order, "SL") == 0 && led == STATUS_LED_ERROR);
    log_ok = true; stream_ok = false; run();
    assert(strcmp(order, "SLAIT") == 0 && led == STATUS_LED_ERROR);
    stream_ok = true; accel_ok = false; run();
    assert(strcmp(order, "SLAITBbX") == 0 && led == STATUS_LED_ERROR);
    puts("PASS: capture/transport before BT and sensor callbacks; startup failures stop safely");
    return 0;
}
