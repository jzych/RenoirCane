#include <limits.h>
#include <string.h>

#include "audio.h"
#include "calibration_protocol.h"
#include "impact.h"
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

void test_impact_event_packet_round_trip_preserves_timestamp_and_outcome(void) {
    calibration_impact_event_v1_t expected = {
        .timestamp_us = 0xFFFFFFF0U,
        .outcome = CAL_IMPACT_PROCESSED,
    };
    uint8_t encoded[CALIBRATION_MAX_ENCODED_PACKET];
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    calibration_packet_view_t packet;

    size_t encoded_length = calibration_packet_encode(
        CAL_PKT_IMPACT_EVENT, (const uint8_t *)&expected, sizeof(expected), encoded, sizeof(encoded));

    TEST_ASSERT_GREATER_THAN_UINT32(0, encoded_length);
    TEST_ASSERT_TRUE(calibration_packet_decode(
        encoded, encoded_length - 1U, raw, sizeof(raw), &packet));
    TEST_ASSERT_EQUAL_UINT8(CAL_PKT_IMPACT_EVENT, packet.packet_type);
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), packet.payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)&expected, packet.payload, sizeof(expected));
}

void test_impact_telemetry_packet_round_trip_preserves_all_counters(void) {
    calibration_impact_telemetry_v1_t expected = {
        .int1_received = 1U,
        .tap_confirmed = 2U,
        .threshold_rejected = 3U,
        .refractory_skipped = 4U,
        .audio_busy_skipped = 5U,
        .event_packets_sent = 6U,
        .event_queue_dropped = 7U,
    };
    uint8_t encoded[CALIBRATION_MAX_ENCODED_PACKET];
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    calibration_packet_view_t packet;

    size_t encoded_length = calibration_packet_encode(
        CAL_PKT_IMPACT_TELEMETRY, (const uint8_t *)&expected, sizeof(expected), encoded, sizeof(encoded));

    TEST_ASSERT_GREATER_THAN_UINT32(0, encoded_length);
    TEST_ASSERT_TRUE(calibration_packet_decode(
        encoded, encoded_length - 1U, raw, sizeof(raw), &packet));
    TEST_ASSERT_EQUAL_UINT8(CAL_PKT_IMPACT_TELEMETRY, packet.packet_type);
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), packet.payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t *)&expected, packet.payload, sizeof(expected));
}

void test_impact_classifier_distinguishes_refractory_skips_from_threshold_rejection(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_AXIS_Y,
        .threshold_raw = 6000,
        .refractory_ms = 200,
    };
    accel_sample_t accepted_sample = {.y = 6001, .timestamp_us = 1000000};
    accel_sample_t below_threshold_sample = {.y = 5999, .timestamp_us = 1100000};
    accel_sample_t refractory_sample = {.y = 6001, .timestamp_us = 1100000};
    impact_event_t event;

    impact_init(&config);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                      impact_classify_candidate(&accepted_sample, accepted_sample.timestamp_us, &event));
    impact_mark_accepted(accepted_sample.timestamp_us);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_BELOW_THRESHOLD,
                      impact_classify_candidate(&below_threshold_sample, below_threshold_sample.timestamp_us, &event));
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_REFRACTORY,
                      impact_classify_candidate(&refractory_sample, refractory_sample.timestamp_us, &event));
}

void test_impact_classifier_accepts_exact_threshold_and_signed_saturation(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_AXIS_Y,
        .threshold_raw = 6000,
        .refractory_ms = 200,
    };
    const int16_t candidates[] = {6000, -6000, INT16_MAX, INT16_MIN};
    const uint32_t expected_magnitudes[] = {6000U, 6000U, 32767U, 32768U};
    impact_event_t event;

    impact_init(&config);
    for (size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        accel_sample_t sample = {.y = candidates[index], .timestamp_us = 1000000 + (int64_t)index};
        TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                          impact_classify_candidate(&sample, sample.timestamp_us, &event));
        TEST_ASSERT_EQUAL_UINT32(expected_magnitudes[index], event.axis_abs_raw);
    }
}

void test_audio_impact_duration_is_available_for_calibration_timing(void) {
    audio_init();

    TEST_ASSERT_GREATER_THAN_UINT32(0, audio_impact_duration_us());
}

void test_audio_busy_rejects_a_candidate_after_its_refractory_period_until_playback_ends(void) {
    int64_t audio_busy_until_us = 0;

    TEST_ASSERT_TRUE(impact_claim_candidate_for_audio(
        IMPACT_CANDIDATE_PROCESSED, 1000000, 500000, &audio_busy_until_us));
    TEST_ASSERT_EQUAL_INT64(1500000, audio_busy_until_us);
    TEST_ASSERT_FALSE(impact_claim_candidate_for_audio(
        IMPACT_CANDIDATE_PROCESSED, 1300000, 500000, &audio_busy_until_us));
    TEST_ASSERT_TRUE(impact_claim_candidate_for_audio(
        IMPACT_CANDIDATE_PROCESSED, 1500000, 500000, &audio_busy_until_us));
}

void test_calibration_metadata_converts_the_impact_threshold_to_the_physical_acceleration(void) {
    float threshold_g = (float)APP_IMPACT_THRESHOLD_RAW * APP_ACCEL_SENSITIVITY_MG_LSB / 1000.0f;

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.928f, threshold_g);
}

void app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cobs_round_trip_preserves_zero_bytes);
    RUN_TEST(test_crc32_matches_iso_hdlc_reference_vector);
    RUN_TEST(test_impact_event_packet_round_trip_preserves_timestamp_and_outcome);
    RUN_TEST(test_impact_telemetry_packet_round_trip_preserves_all_counters);
    RUN_TEST(test_impact_classifier_distinguishes_refractory_skips_from_threshold_rejection);
    RUN_TEST(test_impact_classifier_accepts_exact_threshold_and_signed_saturation);
    RUN_TEST(test_audio_impact_duration_is_available_for_calibration_timing);
    RUN_TEST(test_audio_busy_rejects_a_candidate_after_its_refractory_period_until_playback_ends);
    RUN_TEST(test_calibration_metadata_converts_the_impact_threshold_to_the_physical_acceleration);
    UNITY_END();
}
