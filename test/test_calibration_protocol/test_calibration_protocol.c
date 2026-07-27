#include <string.h>

#include "calibration_protocol.h"
#include "unity.h"

void test_cobs_round_trip_preserves_zero_bytes(void) {
    const uint8_t source[] = {0x11, 0x00, 0x22, 0x00, 0x33};
    uint8_t encoded[16];
    uint8_t decoded[sizeof(source)];
    size_t decoded_length = 0;

    size_t encoded_length = calibration_cobs_encode(source, sizeof(source), encoded, sizeof(encoded));
    TEST_ASSERT_GREATER_THAN_UINT32(0, encoded_length);
    TEST_ASSERT_TRUE(calibration_cobs_decode(encoded, encoded_length, decoded, sizeof(decoded), &decoded_length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(source), decoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source, decoded, sizeof(source));
}

void test_crc32_matches_iso_hdlc_reference_vector(void) {
    const uint8_t source[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, calibration_crc32(source, sizeof(source) - 1));
}

void app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cobs_round_trip_preserves_zero_bytes);
    RUN_TEST(test_crc32_matches_iso_hdlc_reference_vector);
    UNITY_END();
}
