#include "calibration_protocol.h"

#include <string.h>

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

uint32_t calibration_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

size_t calibration_cobs_encode(const uint8_t *input, size_t input_length, uint8_t *output, size_t output_capacity) {
    if ((input == NULL && input_length != 0U) || output == NULL || output_capacity == 0U) return 0U;
    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;
    while (read_index < input_length) {
        if (write_index >= output_capacity) return 0U;
        if (input[read_index] == 0U) {
            output[code_index] = code;
            code = 1U;
            code_index = write_index++;
        } else {
            output[write_index++] = input[read_index];
            if (++code == 0xFFU) {
                output[code_index] = code;
                code = 1U;
                if (write_index >= output_capacity) return 0U;
                code_index = write_index++;
            }
        }
        ++read_index;
    }
    if (code_index >= output_capacity) return 0U;
    output[code_index] = code;
    return write_index;
}

bool calibration_cobs_decode(const uint8_t *input, size_t input_length, uint8_t *output, size_t output_capacity, size_t *output_length) {
    if (input == NULL || output == NULL || output_length == NULL) return false;
    size_t read_index = 0U;
    size_t write_index = 0U;
    while (read_index < input_length) {
        uint8_t code = input[read_index++];
        if (code == 0U || (size_t)(code - 1U) > input_length - read_index) return false;
        for (uint8_t copy = 1U; copy < code; ++copy) {
            if (write_index >= output_capacity) return false;
            output[write_index++] = input[read_index++];
        }
        if (code != 0xFFU && read_index < input_length) {
            if (write_index >= output_capacity) return false;
            output[write_index++] = 0U;
        }
    }
    *output_length = write_index;
    return true;
}

size_t calibration_packet_encode(uint8_t packet_type, const uint8_t *payload, uint16_t payload_length, uint8_t *output, size_t output_capacity) {
    if ((payload == NULL && payload_length != 0U) || payload_length > CALIBRATION_MAX_PAYLOAD || output == NULL || output_capacity < 2U) return 0U;
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    raw[0] = CALIBRATION_PROTOCOL_VERSION;
    raw[1] = packet_type;
    raw[2] = (uint8_t)payload_length;
    raw[3] = (uint8_t)(payload_length >> 8);
    if (payload_length != 0U) memcpy(&raw[4], payload, payload_length);
    uint32_t crc = calibration_crc32(raw, 4U + payload_length);
    raw[4U + payload_length] = (uint8_t)crc;
    raw[5U + payload_length] = (uint8_t)(crc >> 8);
    raw[6U + payload_length] = (uint8_t)(crc >> 16);
    raw[7U + payload_length] = (uint8_t)(crc >> 24);
    size_t encoded_length = calibration_cobs_encode(raw, 8U + payload_length, output, output_capacity - 1U);
    if (encoded_length == 0U) return 0U;
    output[encoded_length] = 0U;
    return encoded_length + 1U;
}

bool calibration_packet_decode(const uint8_t *encoded, size_t encoded_length, uint8_t *raw_scratch, size_t raw_scratch_capacity, calibration_packet_view_t *packet) {
    if (encoded == NULL || raw_scratch == NULL || packet == NULL) return false;
    size_t raw_length = 0U;
    if (!calibration_cobs_decode(encoded, encoded_length, raw_scratch, raw_scratch_capacity, &raw_length) || raw_length < 8U) return false;
    if (raw_scratch[0] != CALIBRATION_PROTOCOL_VERSION) return false;
    uint16_t payload_length = (uint16_t)raw_scratch[2] | ((uint16_t)raw_scratch[3] << 8);
    if (payload_length > CALIBRATION_MAX_PAYLOAD || raw_length != (size_t)payload_length + 8U) return false;
    if (calibration_crc32(raw_scratch, 4U + payload_length) != read_u32_le(&raw_scratch[4U + payload_length])) return false;
    packet->packet_type = raw_scratch[1];
    packet->payload = &raw_scratch[4];
    packet->payload_length = payload_length;
    return true;
}
