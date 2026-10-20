#include "calibration_stream.h"

#include "app_config.h"
#include "accelerometer.h"
#include "calibration_protocol.h"
#include "bluetooth_audio.h"
#include "impact.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define CALIBRATION_QUEUE_CAPACITY 128U
#define CALIBRATION_IMPACT_QUEUE_CAPACITY 32U
#define CALIBRATION_UART_BAUD_RATE 921600U

typedef struct {
    uint32_t int1_received;
    uint32_t tap_confirmed;
    uint32_t threshold_rejected;
    uint32_t refractory_skipped;
    uint32_t audio_busy_skipped;
    uint32_t event_packets_sent;
    uint32_t event_queue_dropped;
} impact_telemetry_counters_t;

static QueueHandle_t s_sample_queue;
static QueueHandle_t s_impact_event_queue;
static TaskHandle_t s_sample_task;
static bool s_tap_pending;
static uint32_t s_next_sequence;
static uint32_t s_samples_acquired;
static uint32_t s_samples_transmitted;
static uint32_t s_samples_dropped;
static impact_telemetry_counters_t s_impact_telemetry;
static portMUX_TYPE s_impact_telemetry_lock = portMUX_INITIALIZER_UNLOCKED;

static bool write_packet(uint8_t packet_type, const void *payload, uint16_t payload_length);

static void impact_telemetry_increment(uint32_t *counter) {
    portENTER_CRITICAL(&s_impact_telemetry_lock);
    ++*counter;
    portEXIT_CRITICAL(&s_impact_telemetry_lock);
}

static void IRAM_ATTR note_tap_interrupt_from_isr(void) {
    portENTER_CRITICAL_ISR(&s_impact_telemetry_lock);
    ++s_impact_telemetry.int1_received;
    s_tap_pending = true;
    portEXIT_CRITICAL_ISR(&s_impact_telemetry_lock);
}

static bool take_pending_tap(void) {
    portENTER_CRITICAL(&s_impact_telemetry_lock);
    bool pending = s_tap_pending;
    s_tap_pending = false;
    portEXIT_CRITICAL(&s_impact_telemetry_lock);
    return pending;
}

static calibration_impact_telemetry_v1_t impact_telemetry_snapshot(void) {
    impact_telemetry_counters_t counters;
    portENTER_CRITICAL(&s_impact_telemetry_lock);
    counters = s_impact_telemetry;
    portEXIT_CRITICAL(&s_impact_telemetry_lock);
    return (calibration_impact_telemetry_v1_t){
        .int1_received = counters.int1_received,
        .tap_confirmed = counters.tap_confirmed,
        .threshold_rejected = counters.threshold_rejected,
        .refractory_skipped = counters.refractory_skipped,
        .audio_busy_skipped = counters.audio_busy_skipped,
        .event_packets_sent = counters.event_packets_sent,
        .event_queue_dropped = counters.event_queue_dropped,
    };
}

static void process_commands(const calibration_metadata_v1_t *metadata) {
    static uint8_t frame[CALIBRATION_MAX_ENCODED_PACKET];
    static size_t frame_length;
    uint8_t incoming[64];
    int count = uart_read_bytes(UART_NUM_0, incoming, sizeof(incoming), 0U);
    for (int index = 0; index < count; ++index) {
        if (incoming[index] == 0U) {
            uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
            calibration_packet_view_t packet;
            if (frame_length != 0U && calibration_packet_decode(frame, frame_length, raw, sizeof(raw), &packet) && packet.packet_type == CAL_CMD_GET_METADATA && packet.payload_length == 0U) {
                (void)write_packet(CAL_PKT_METADATA, metadata, sizeof(*metadata));
            }
            frame_length = 0U;
        } else if (frame_length < sizeof(frame)) {
            frame[frame_length++] = incoming[index];
        } else {
            frame_length = 0U;
        }
    }
}

static bool write_packet(uint8_t packet_type, const void *payload, uint16_t payload_length) {
    uint8_t encoded[CALIBRATION_MAX_ENCODED_PACKET];
    size_t length = calibration_packet_encode(packet_type, payload, payload_length, encoded, sizeof(encoded));
    return length != 0U && uart_write_bytes(UART_NUM_0, encoded, length) == (int)length;
}

static void emit_impact_outcome(const accel_sample_t *sample, bool tap_confirmed) {
    impact_event_t candidate;
    impact_candidate_result_t result = impact_process_sample(sample, tap_confirmed, bluetooth_audio_start_impact, &candidate);
    if (result == IMPACT_CANDIDATE_NO_TAP) return;
    if (result == IMPACT_CANDIDATE_BELOW_THRESHOLD) {
        impact_telemetry_increment(&s_impact_telemetry.threshold_rejected);
        return;
    }

    calibration_impact_event_v1_t event = {
        .timestamp_us = (uint32_t)candidate.timestamp_us,
        .outcome = CAL_IMPACT_SKIPPED,
    };
    if (result == IMPACT_CANDIDATE_REFRACTORY) {
        impact_telemetry_increment(&s_impact_telemetry.refractory_skipped);
    } else if (result == IMPACT_CANDIDATE_AUDIO_BUSY) {
        impact_telemetry_increment(&s_impact_telemetry.audio_busy_skipped);
    } else if (result == IMPACT_CANDIDATE_PROCESSED) {
        event.outcome = CAL_IMPACT_PROCESSED;
    }
    if (xQueueSend(s_impact_event_queue, &event, 0U) != pdPASS) {
        impact_telemetry_increment(&s_impact_telemetry.event_queue_dropped);
    }
}

static void sample_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* A tap arriving during acquisition belongs to the next read. */
        bool tap_pending = take_pending_tap();
        accel_sample_t raw;
        bool tap_confirmed;
        bool sample_read = impact_read_sample(tap_pending, &raw, &tap_confirmed);
        if (tap_confirmed) impact_telemetry_increment(&s_impact_telemetry.tap_confirmed);
        /* A failed post-tap read consumes that tap, just as in old normal mode. */
        if (!sample_read) continue;
        emit_impact_outcome(&raw, tap_confirmed);
        calibration_sample_t sample = {
            .seq = s_next_sequence++,
            .timestamp_us = (uint32_t)raw.timestamp_us,
            .x_raw = raw.x,
            .y_raw = raw.y,
            .z_raw = raw.z,
        };
        ++s_samples_acquired;
        if (xQueueSend(s_sample_queue, &sample, 0U) != pdPASS) ++s_samples_dropped;
    }
}

static void transport_task(void *arg) {
    (void)arg;
    calibration_metadata_v1_t metadata = {
        .sample_rate_hz = APP_ACCEL_ODR_HZ,
        .range_g = APP_ACCEL_RANGE_G,
        .impact_axis = APP_IMPACT_ACTIVE_AXIS,
        .impact_threshold_raw = APP_IMPACT_THRESHOLD_RAW,
        .sensitivity_mg_lsb = APP_ACCEL_SENSITIVITY_MG_LSB,
        .int1_gpio = APP_ACCEL_INT1_GPIO,
        .int2_gpio = APP_ACCEL_INT2_GPIO,
        .sample_format_version = 1U,
        .reserved = 0U,
    };
    (void)write_packet(CAL_PKT_METADATA, &metadata, sizeof(metadata));
    TickType_t last_status_tick = xTaskGetTickCount();
    for (;;) {
        process_commands(&metadata);
        calibration_sample_batch_v1_t batch = {.count = 0U};
        if (xQueueReceive(s_sample_queue, &batch.samples[0], pdMS_TO_TICKS(100)) == pdPASS) {
            batch.count = 1U;
            while (batch.count < CALIBRATION_SAMPLE_BATCH_CAPACITY && xQueueReceive(s_sample_queue, &batch.samples[batch.count], 0U) == pdPASS) ++batch.count;
            size_t bytes = sizeof(batch.count) + ((size_t)batch.count * sizeof(batch.samples[0]));
            if (write_packet(CAL_PKT_SAMPLE_BATCH, &batch, (uint16_t)bytes)) s_samples_transmitted += batch.count;
        }
        calibration_impact_event_v1_t impact_event;
        while (xQueueReceive(s_impact_event_queue, &impact_event, 0U) == pdPASS) {
            if (write_packet(CAL_PKT_IMPACT_EVENT, &impact_event, sizeof(impact_event))) {
                impact_telemetry_increment(&s_impact_telemetry.event_packets_sent);
            }
        }
        if ((xTaskGetTickCount() - last_status_tick) >= pdMS_TO_TICKS(1000)) {
            calibration_status_v1_t status = {
                .samples_acquired = s_samples_acquired,
                .samples_transmitted = s_samples_transmitted,
                .samples_dropped = s_samples_dropped,
                .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL),
            };
            (void)write_packet(CAL_PKT_STATUS, &status, sizeof(status));
            calibration_impact_telemetry_v1_t telemetry = impact_telemetry_snapshot();
            (void)write_packet(CAL_PKT_IMPACT_TELEMETRY, &telemetry, sizeof(telemetry));
            last_status_tick = xTaskGetTickCount();
        }
    }
}

bool calibration_stream_start(void) {
    s_sample_queue = xQueueCreate(CALIBRATION_QUEUE_CAPACITY, sizeof(calibration_sample_t));
    s_impact_event_queue = xQueueCreate(CALIBRATION_IMPACT_QUEUE_CAPACITY, sizeof(calibration_impact_event_v1_t));
    if (s_sample_queue == NULL || s_impact_event_queue == NULL) return false;
    if (!uart_is_driver_installed(UART_NUM_0) && uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0) != ESP_OK) return false;
    if (uart_set_baudrate(UART_NUM_0, CALIBRATION_UART_BAUD_RATE) != ESP_OK) return false;
    if (xTaskCreate(sample_task, "cal_sample", 3072U, NULL, 8U, &s_sample_task) != pdPASS) return false;
    return xTaskCreate(transport_task, "cal_transport", 4096U, NULL, 7U, NULL) == pdPASS;
}

void IRAM_ATTR calibration_stream_on_data_ready_from_isr(void) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (s_sample_task != NULL) vTaskNotifyGiveFromISR(s_sample_task, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) portYIELD_FROM_ISR();
}

void IRAM_ATTR calibration_stream_on_impact_from_isr(void) {
    note_tap_interrupt_from_isr();
    /* Wake immediately for the same post-tap read as normal mode, including
       when INT2 has no new notification yet. */
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (s_sample_task != NULL) vTaskNotifyGiveFromISR(s_sample_task, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) portYIELD_FROM_ISR();
}
