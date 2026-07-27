#include "calibration_stream.h"

#include "app_config.h"
#include "accelerometer.h"
#include "calibration_protocol.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define CALIBRATION_QUEUE_CAPACITY 128U
#define CALIBRATION_UART_BAUD_RATE 921600U

static QueueHandle_t s_sample_queue;
static TaskHandle_t s_sample_task;
static uint32_t s_next_sequence;
static uint32_t s_samples_acquired;
static uint32_t s_samples_transmitted;
static uint32_t s_samples_dropped;

static bool write_packet(uint8_t packet_type, const void *payload, uint16_t payload_length);

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

static void sample_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        accel_sample_t raw;
        if (!accelerometer_read_sample(&raw)) continue;
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
        if ((xTaskGetTickCount() - last_status_tick) >= pdMS_TO_TICKS(1000)) {
            calibration_status_v1_t status = {
                .samples_acquired = s_samples_acquired,
                .samples_transmitted = s_samples_transmitted,
                .samples_dropped = s_samples_dropped,
                .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL),
            };
            (void)write_packet(CAL_PKT_STATUS, &status, sizeof(status));
            last_status_tick = xTaskGetTickCount();
        }
    }
}

bool calibration_stream_start(void) {
    s_sample_queue = xQueueCreate(CALIBRATION_QUEUE_CAPACITY, sizeof(calibration_sample_t));
    if (s_sample_queue == NULL) return false;
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
