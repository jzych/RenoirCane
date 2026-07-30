#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CALIBRATION_PROTOCOL_VERSION 1U
#define CALIBRATION_MAX_PAYLOAD 256U
#define CALIBRATION_MAX_RAW_PACKET (4U + CALIBRATION_MAX_PAYLOAD + 4U)
#define CALIBRATION_MAX_ENCODED_PACKET (CALIBRATION_MAX_RAW_PACKET + (CALIBRATION_MAX_RAW_PACKET / 254U) + 2U)
#define CALIBRATION_SAMPLE_BATCH_CAPACITY 16U

typedef enum {
    CAL_PKT_METADATA = 0x01,
    CAL_PKT_SAMPLE_BATCH = 0x02,
    CAL_PKT_STATUS = 0x03,
    CAL_PKT_ERROR = 0x04,
    CAL_PKT_IMPACT_EVENT = 0x05,
    CAL_CMD_GET_METADATA = 0x80,
} calibration_packet_type_t;

typedef enum {
    CAL_IMPACT_PROCESSED = 0x01,
    CAL_IMPACT_SKIPPED = 0x02,
} calibration_impact_outcome_t;

typedef enum {
    CAL_AXIS_X = 0,
    CAL_AXIS_Y = 1,
    CAL_AXIS_Z = 2,
} calibration_axis_t;

typedef struct __attribute__((packed)) {
    uint16_t sample_rate_hz;
    uint8_t range_g;
    uint8_t impact_axis;
    int16_t impact_threshold_raw;
    float sensitivity_mg_lsb;
    uint8_t int1_gpio;
    uint8_t int2_gpio;
    uint8_t sample_format_version;
    uint8_t reserved;
} calibration_metadata_v1_t;

typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint32_t timestamp_us;
    int16_t x_raw;
    int16_t y_raw;
    int16_t z_raw;
} calibration_sample_t;

typedef struct __attribute__((packed)) {
    uint8_t count;
    calibration_sample_t samples[CALIBRATION_SAMPLE_BATCH_CAPACITY];
} calibration_sample_batch_v1_t;

typedef struct __attribute__((packed)) {
    uint32_t samples_acquired;
    uint32_t samples_transmitted;
    uint32_t samples_dropped;
    uint32_t uptime_ms;
} calibration_status_v1_t;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_us;
    uint8_t outcome;
} calibration_impact_event_v1_t;

typedef struct __attribute__((packed)) {
    uint16_t error_code;
    int32_t detail;
} calibration_error_v1_t;

typedef struct {
    uint8_t packet_type;
    const uint8_t *payload;
    uint16_t payload_length;
} calibration_packet_view_t;

uint32_t calibration_crc32(const uint8_t *data, size_t length);
size_t calibration_cobs_encode(const uint8_t *input, size_t input_length, uint8_t *output, size_t output_capacity);
bool calibration_cobs_decode(const uint8_t *input, size_t input_length, uint8_t *output, size_t output_capacity, size_t *output_length);
size_t calibration_packet_encode(uint8_t packet_type, const uint8_t *payload, uint16_t payload_length, uint8_t *output, size_t output_capacity);
bool calibration_packet_decode(const uint8_t *encoded, size_t encoded_length, uint8_t *raw_scratch, size_t raw_scratch_capacity, calibration_packet_view_t *packet);
