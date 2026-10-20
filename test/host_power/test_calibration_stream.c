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
static bool hardware_tap;
static unsigned tap_notifications;
static char io_order[32];
static unsigned io_count;
static accel_sample_t next_sample;
static bt_audio_play_result_t playback_result;
static calibration_impact_event_v1_t last_event;
static bool fail_tx_drain;
static unsigned baud_changes, drain_calls;
static bool transport_running, log_pending;
static unsigned transport_reads;
static uint8_t wire[4096];
static size_t wire_length;
static jmp_buf transport_finished;
static bool transport_sample_pending, transport_event_pending;
static unsigned clock_reads;
static uint32_t log_dropped, drops_during_write;
static bool fail_write, short_write;

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack,
                      void *arg, UBaseType_t priority, TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    if (out) *out = (void *)3;
    return pdPASS;
}
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken) {
    assert(task == s_sample_task);
    ++tap_notifications;
    *woken = pdFALSE;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) {
    (void)clear; (void)wait;
    if (waits++ > 0) longjmp(task_finished, 1);
    return 1;
}
TickType_t xTaskGetTickCount(void) {
    return transport_running && clock_reads++ > 0 ? 1000 : 0;
}
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
    (void)wait;
    if (transport_running && queue == s_sample_queue && transport_sample_pending) {
        *(calibration_sample_t *)item = (calibration_sample_t){.seq = 10, .timestamp_us = 1000, .y_raw = 4500};
        transport_sample_pending = false;
        return pdPASS;
    }
    if (transport_running && queue == s_impact_event_queue && transport_event_pending) {
        *(calibration_impact_event_v1_t *)item = (calibration_impact_event_v1_t){.timestamp_us = 1000, .outcome = CAL_IMPACT_PROCESSED};
        transport_event_pending = false;
        return pdPASS;
    }
    return pdFALSE;
}
bool uart_is_driver_installed(int port) { (void)port; return true; }
esp_err_t uart_driver_install(int port, int rx, int tx, int size, void *queue, int flags) {
    (void)port; (void)rx; (void)tx; (void)size; (void)queue; (void)flags; return ESP_OK;
}
esp_err_t uart_wait_tx_done(int port, TickType_t wait) {
    assert(port == 0 && wait == 1000);
    ++drain_calls;
    return fail_tx_drain ? ESP_FAIL : ESP_OK;
}
esp_err_t uart_set_baudrate(int port, uint32_t baud) {
    assert(port == 0 && baud == 921600);
    assert(drain_calls > 0 && !fail_tx_drain);
    ++baud_changes;
    return ESP_OK;
}
int uart_read_bytes(int port, void *buffer, uint32_t length, TickType_t wait) {
    (void)port; (void)buffer; (void)length; (void)wait;
    if (transport_running && transport_reads++ == 1) longjmp(transport_finished, 1);
    return 0;
}
int uart_write_bytes(int port, const void *buffer, size_t length) {
    assert(port == 0 && wire_length + length <= sizeof(wire));
    log_dropped += drops_during_write;
    drops_during_write = 0;
    if (fail_write) return -1;
    if (short_write) return (int)length - 1;
    memcpy(wire + wire_length, buffer, length);
    wire_length += length;
    return (int)length;
}
size_t calibration_log_read(uint8_t *buffer, size_t capacity) {
    const char text[] = "BT disconnected\nprintf\n";
    if (!log_pending) return 0;
    assert(capacity >= sizeof(text) - 1);
    memcpy(buffer, text, sizeof(text) - 1);
    log_pending = false;
    return sizeof(text) - 1;
}
uint32_t calibration_log_take_dropped_bytes(void) {
    uint32_t dropped = log_dropped;
    log_dropped = 0;
    return dropped;
}
void calibration_log_record_dropped_bytes(size_t count) {
    if (count > UINT32_MAX - log_dropped) log_dropped = UINT32_MAX;
    else log_dropped += (uint32_t)count;
}
bool accelerometer_read_sample(accel_sample_t *sample) {
    ++reads;
    io_order[io_count++] = 'R';
    if (!fail_read) *sample = next_sample;
    if (interrupt_during_read) {
        interrupt_during_read = false;
        calibration_stream_on_impact_from_isr();
    }
    return !fail_read;
}
bool accelerometer_read_impact_status(void) {
    ++tap_status_reads;
    io_order[io_count++] = 'S';
    return hardware_tap;
}
void accelerometer_clear_interrupt(void) { io_order[io_count++] = 'C'; }
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
    hardware_tap = true;
    tap_notifications = 0;
    playback_result = BT_AUDIO_PLAY_STARTED;
    assert(calibration_stream_start());
}

static void feed(int16_t y, int64_t timestamp_us) {
    next_sample = (accel_sample_t){.y = y, .timestamp_us = timestamp_us};
    memset(io_order, 0, sizeof(io_order));
    io_count = 0;
    waits = 0;
    if (setjmp(task_finished) == 0) sample_task(NULL);
}

int main(void) {
    fail_tx_drain = true;
    assert(!calibration_stream_start());
    assert(baud_changes == 0);
    fail_tx_drain = false;
    reset();
    feed(-32768, 1000000); /* A stream spike cannot bypass the hardware tap gate. */
    assert(playback_calls == 0 && events == 0);
    assert(strcmp(io_order, "R") == 0);
    calibration_stream_on_impact_from_isr();
    feed(4500, 1000001); /* Ungated spikes must not consume refractory. */
    assert(playback_calls == 1 && events == 1);
    assert(strcmp(io_order, "SCR") == 0); /* The old normal-mode read order. */

    reset();
    interrupt_during_read = true;
    feed(4499, 1);
    assert(s_impact_telemetry.int1_received == 1);
    assert(s_impact_telemetry.tap_confirmed == 0);
    assert(s_impact_telemetry.threshold_rejected == 0);
    assert(tap_status_reads == 0); /* A new IRQ cannot classify a pre-IRQ sample. */
    assert(tap_notifications == 1);
    feed(4499, 2);
    assert(s_impact_telemetry.tap_confirmed == 1);
    assert(s_impact_telemetry.threshold_rejected == 1);
    assert(reads == 2 && tap_status_reads == 1);
    assert(strcmp(io_order, "SCR") == 0);

    reset();
    hardware_tap = false;
    calibration_stream_on_impact_from_isr();
    feed(-32768, 1);
    assert(s_impact_telemetry.int1_received == 1 && s_impact_telemetry.tap_confirmed == 0);
    assert(playback_calls == 0 && events == 0);
    assert(strcmp(io_order, "SCR") == 0);

    reset();
    calibration_stream_on_impact_from_isr();
    fail_read = true;
    feed(4500, 1);
    assert(s_impact_telemetry.tap_confirmed == 1 && s_samples_acquired == 0);
    assert(events == 0 && playback_calls == 0);
    fail_read = false;
    feed(32767, 2); /* A cleared tap must not arm a later unrelated sample. */
    assert(s_impact_telemetry.int1_received == 1 && s_impact_telemetry.tap_confirmed == 1);
    assert(s_impact_telemetry.threshold_rejected == 0 && reads == 2);
    assert(playback_calls == 0 && events == 0 && tap_status_reads == 1);

    reset();
    calibration_stream_on_impact_from_isr();
    feed(4500, 1000000);
    assert(playback_calls == 1 && events == 1);
    assert(last_event.outcome == CAL_IMPACT_PROCESSED && last_event.timestamp_us == 1000000);
    feed(32767, 1300000); /* Stream values alone cannot repeat a cue. */
    assert(playback_calls == 1 && events == 1);
    calibration_stream_on_impact_from_isr();
    feed(32767, 1300001); /* A distinct tap needs no software crossing/rearm. */
    assert(playback_calls == 2 && events == 2);
    playback_result = BT_AUDIO_PLAY_BUSY;
    calibration_stream_on_impact_from_isr();
    feed(-32768, 1600002);
    assert(events == 3 && last_event.outcome == CAL_IMPACT_SKIPPED);
    assert(s_impact_telemetry.audio_busy_skipped == 1);
    feed(0, 1600003);
    calibration_stream_on_impact_from_isr();
    feed(4500, 1600004);
    assert(events == 4 && playback_calls == 3);
    assert(s_impact_telemetry.refractory_skipped == 1);
    playback_result = BT_AUDIO_PLAY_DISCONNECTED;
    calibration_stream_on_impact_from_isr();
    feed(4500, 1900001);
    assert(events == 5 && last_event.outcome == CAL_IMPACT_SKIPPED);
    assert(s_impact_telemetry.audio_busy_skipped == 1);
    assert(s_impact_telemetry.threshold_rejected == 0); /* Stream lows aren't tap rejections. */
    drop_event = true;
    calibration_stream_on_impact_from_isr();
    feed(4500, 2200001);
    assert(s_impact_telemetry.event_queue_dropped == 1);
    assert(reads == 8 && s_samples_acquired == 8); /* Exactly one output read per acquisition. */

    wire_length = 0; transport_reads = 0;
    clock_reads = 0;
    s_impact_telemetry.event_packets_sent = 0;
    transport_running = log_pending = true;
    log_dropped = 8;
    transport_sample_pending = transport_event_pending = true;
    if (setjmp(transport_finished) == 0) transport_task(NULL);
    assert(!log_pending && wire_length > 1 && wire[0] == 0);
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    calibration_packet_view_t packet;
    unsigned packets = 0;
    const uint8_t types[] = {1, 2, 5, 3, 6, 7};
    for (size_t start = 1, end = 1; end < wire_length; ++end) {
        if (wire[end] != 0) continue;
        assert(calibration_packet_decode(wire + start, end - start, raw, sizeof(raw), &packet));
        assert(packets < sizeof(types) && packet.packet_type == types[packets]);
        if (packet.packet_type == 6) {
            calibration_impact_telemetry_v1_t telemetry;
            memcpy(&telemetry, packet.payload, sizeof(telemetry));
            assert(telemetry.event_packets_sent == 1);
        }
        if (packet.packet_type == 7) {
            const char text[] = "\n[firmware logs dropped: 8 bytes]\nBT disconnected\nprintf\n";
            assert(packet.payload_length == sizeof(text) - 1);
            assert(memcmp(packet.payload, text, sizeof(text) - 1) == 0);
        }
        ++packets;
        start = end + 1;
    }
    assert(packets == 6 && !transport_sample_pending && !transport_event_pending);
    assert(s_samples_transmitted >= 1 && s_impact_telemetry.event_packets_sent == 1);
    /* A failed send must preserve the old notice and account for the 23 text
       bytes consumed from the ring, including losses produced during the write. */
    wire_length = 0;
    log_pending = fail_write = true;
    log_dropped = 8;
    drops_during_write = 5;
    emit_log_packet();
    assert(!log_pending && log_dropped == 36 && wire_length == 0);
    emit_log_packet(); /* A failed notice-only packet must not inflate the count. */
    assert(log_dropped == 36);
    fail_write = false;
    emit_log_packet();
    assert(log_dropped == 0 && wire_length > 0);
    assert(calibration_packet_decode(wire, wire_length - 1, raw, sizeof(raw), &packet));
    const char loss_notice[] = "\n[firmware logs dropped: 36 bytes]\n";
    assert(packet.packet_type == CAL_PKT_LOG && packet.payload_length == sizeof(loss_notice) - 1);
    assert(memcmp(packet.payload, loss_notice, sizeof(loss_notice) - 1) == 0);

    wire_length = 0;
    log_pending = short_write = true;
    log_dropped = UINT32_MAX - 10;
    emit_log_packet(); /* Conservatively count the whole text on a short write. */
    assert(!log_pending && log_dropped == UINT32_MAX);
    short_write = false;
    emit_log_packet();
    assert(log_dropped == 0);
    assert(calibration_packet_decode(wire, wire_length - 1, raw, sizeof(raw), &packet));
    const char saturated_notice[] = "\n[firmware logs dropped: 4294967295 bytes]\n";
    assert(packet.payload_length == sizeof(saturated_notice) - 1);
    assert(memcmp(packet.payload, saturated_notice, sizeof(saturated_notice) - 1) == 0);
    puts("PASS: UART drain failure, delimiter/metadata/framed logs, tap-gated cues/markers and queue drops");
    return 0;
}
