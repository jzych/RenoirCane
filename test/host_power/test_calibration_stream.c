#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "../../components/calibration/calibration_stream.c"

/* Exercise the actual sampling task and decision function; replace hardware,
   scheduling and queues so an interrupt can arrive during the sensor read. */
static jmp_buf task_finished;
static unsigned waits, reads, playback_calls, events, tap_status_reads;
static bool interrupt_during_read, fail_read, drop_event;
static accel_sample_t next_sample;
static bt_audio_play_result_t playback_result;
static calibration_impact_event_v1_t last_event;

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack,
                      void *arg, UBaseType_t priority, TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    if (out) *out = (void *)3;
    return pdPASS;
}
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) { (void)task; *woken = pdFALSE; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    (void)clear; (void)wait;
    if (waits++ > 0) longjmp(task_finished, 1);
    return 1;
}
TickType_t xTaskGetTickCount(void) { return 0; }
int64_t esp_timer_get_time(void) { return next_sample.timestamp_us; }
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t size) {
    (void)capacity;
    return size == sizeof(calibration_sample_t) ? (void *)1 : (void *)2;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    (void)wait;
    if (queue == s_impact_event_queue) {
        if (drop_event) return pdFALSE;
        last_event = *(const calibration_impact_event_v1_t *)item;
        ++events;
    } else {
        const calibration_sample_t *sample = item;
        assert(sample->timestamp_us == (uint32_t)next_sample.timestamp_us);
        assert(sample->y_raw == next_sample.y);
    }
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    (void)queue; (void)item; (void)wait; return pdFALSE;
}
bool uart_is_driver_installed(int port) { (void)port; return true; }
esp_err_t uart_driver_install(int port, int rx, int tx, int size, void *queue, int flags) {
    (void)port; (void)rx; (void)tx; (void)size; (void)queue; (void)flags; return ESP_OK;
}
esp_err_t uart_set_baudrate(int port, uint32_t baud) { (void)port; (void)baud; return ESP_OK; }
int uart_read_bytes(int port, void *buffer, uint32_t length, TickType_t wait) {
    (void)port; (void)buffer; (void)length; (void)wait; return 0;
}
int uart_write_bytes(int port, const void *buffer, size_t length) { (void)port; (void)buffer; return (int)length; }
bool accelerometer_read_sample(accel_sample_t *sample) {
    ++reads;
    *sample = next_sample;
    if (interrupt_during_read) {
        interrupt_during_read = false;
        calibration_stream_on_impact_from_isr();
    }
    return !fail_read;
}
bool accelerometer_read_impact_status(void) { ++tap_status_reads; return true; }
void accelerometer_clear_interrupt(void) {}
bt_audio_play_result_t bluetooth_audio_start_impact(void) { ++playback_calls; return playback_result; }

static void reset(void) {
    impact_config_t config = {.axis = APP_IMPACT_AXIS_Y, .threshold_raw = 4500, .refractory_ms = 200};
    impact_init(&config);
    memset(&s_impact_telemetry, 0, sizeof(s_impact_telemetry));
    s_tap_pending = false;
    s_samples_acquired = 0;
    s_samples_dropped = 0;
    reads = playback_calls = events = tap_status_reads = 0;
    interrupt_during_read = fail_read = drop_event = false;
    playback_result = BT_AUDIO_PLAY_STARTED;
    assert(calibration_stream_start());
}

static void feed(int16_t y, int64_t timestamp_us) {
    next_sample = (accel_sample_t){.y = y, .timestamp_us = timestamp_us};
    waits = 0;
    if (setjmp(task_finished) == 0) sample_task(NULL);
}

int main(void) {
    reset();
    interrupt_during_read = true;
    feed(4499, 1);
    assert(s_impact_telemetry.int1_received == 1);
    assert(s_impact_telemetry.tap_confirmed == 0);
    assert(s_impact_telemetry.threshold_rejected == 0);
    assert(tap_status_reads == 0); /* A new IRQ cannot classify a pre-IRQ sample. */
    feed(4499, 2);
    assert(s_impact_telemetry.tap_confirmed == 1);
    assert(s_impact_telemetry.threshold_rejected == 1);
    assert(reads == 2 && tap_status_reads == 1);

    reset();
    calibration_stream_on_impact_from_isr();
    fail_read = true;
    feed(4500, 1);
    assert(s_impact_telemetry.tap_confirmed == 0 && s_samples_acquired == 0);
    assert(events == 0 && playback_calls == 0);
    fail_read = false;
    feed(4499, 2);
    assert(s_impact_telemetry.int1_received == 1 && s_impact_telemetry.tap_confirmed == 1);
    assert(s_impact_telemetry.threshold_rejected == 1 && reads == 2);

    reset();
    feed(4500, 1000000); /* No tap prerequisite. */
    assert(playback_calls == 1 && events == 1);
    assert(last_event.outcome == CAL_IMPACT_PROCESSED && last_event.timestamp_us == 1000000);
    feed(32767, 1300000); /* Held crossing cannot repeat. */
    assert(playback_calls == 1 && events == 1);
    feed(0, 1300001);
    playback_result = BT_AUDIO_PLAY_BUSY;
    feed(-32768, 1300002);
    assert(events == 2 && last_event.outcome == CAL_IMPACT_SKIPPED);
    assert(s_impact_telemetry.audio_busy_skipped == 1);
    feed(0, 1300003);
    feed(4500, 1300004);
    assert(events == 3 && playback_calls == 2);
    assert(s_impact_telemetry.refractory_skipped == 1);
    feed(0, 1600000);
    playback_result = BT_AUDIO_PLAY_DISCONNECTED;
    feed(4500, 1600001);
    assert(events == 4 && last_event.outcome == CAL_IMPACT_SKIPPED);
    assert(s_impact_telemetry.audio_busy_skipped == 1);
    assert(s_impact_telemetry.threshold_rejected == 0); /* Stream lows aren't tap rejections. */
    feed(0, 1900000);
    drop_event = true;
    feed(4500, 1900001);
    assert(s_impact_telemetry.event_queue_dropped == 1);
    assert(reads == 10 && s_samples_acquired == 10); /* Exactly one output read per sample. */

    puts("PASS: stream decisions/markers, one sensor reader, deferred tap diagnostics, queue drops");
    return 0;
}
