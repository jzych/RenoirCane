#include <limits.h>
#include <string.h>

#include "audio.h"
#include "calibration_playback.h"
#include "bluetooth_audio_policy.h"
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

void test_impact_classifier_accepts_the_initial_candidate_without_timestamp_underflow(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_AXIS_Y,
        .threshold_raw = 6000,
        .refractory_ms = 200,
    };
    accel_sample_t initial_sample = {.y = 6000, .timestamp_us = 1};
    impact_event_t event;

    impact_init(&config);

    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                      impact_classify_candidate(&initial_sample, initial_sample.timestamp_us, &event));
}

void test_impact_classifier_accepts_exact_threshold_and_signed_saturation(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_AXIS_Y,
        .threshold_raw = 6000,
        .refractory_ms = 200,
    };
    const int16_t candidates[] = {6000, -6000, INT16_MAX, INT16_MIN};
    const uint32_t expected_magnitudes[] = {6000U, 6000U, 32767U, 32768U};
    accel_sample_t below_threshold = {.y = 5999, .timestamp_us = 999999};
    impact_event_t event;

    impact_init(&config);
    TEST_ASSERT_FALSE(impact_sample_exceeds_threshold(&below_threshold));
    for (size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        accel_sample_t sample = {.y = candidates[index], .timestamp_us = 1000000 + (int64_t)index};
        TEST_ASSERT_TRUE(impact_sample_exceeds_threshold(&sample));
        TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                          impact_classify_candidate(&sample, sample.timestamp_us, &event));
        TEST_ASSERT_EQUAL_UINT32(expected_magnitudes[index], event.axis_abs_raw);
    }
}

void test_impact_threshold_crossing_emits_once_until_the_signal_drops_below_threshold(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_AXIS_Y,
        .threshold_raw = 6000,
        .refractory_ms = 200,
    };
    impact_event_t event;
    accel_sample_t below = {.y = 5999, .timestamp_us = 1000000};
    accel_sample_t crossing = {.y = 6000, .timestamp_us = 1010000};
    accel_sample_t held_above = {.y = INT16_MAX, .timestamp_us = 1300000};
    accel_sample_t dropped_below = {.y = 5999, .timestamp_us = 1310000};
    accel_sample_t rearmed_crossing = {.y = 6000, .timestamp_us = 1320000};

    impact_init(&config);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_BELOW_THRESHOLD,
                      impact_classify_threshold_crossing(&below, below.timestamp_us, &event));
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                      impact_classify_threshold_crossing(&crossing, crossing.timestamp_us, &event));
    impact_mark_accepted(crossing.timestamp_us);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_BELOW_THRESHOLD,
                      impact_classify_threshold_crossing(&held_above, held_above.timestamp_us, &event));
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_BELOW_THRESHOLD,
                      impact_classify_threshold_crossing(&dropped_below, dropped_below.timestamp_us, &event));
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                      impact_classify_threshold_crossing(&rearmed_crossing, rearmed_crossing.timestamp_us, &event));
}

void test_impact_threshold_crossing_preserves_refractory_skip_outcomes(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_AXIS_Y,
        .threshold_raw = 6000,
        .refractory_ms = 200,
    };
    impact_event_t event;
    accel_sample_t first_crossing = {.y = 6000, .timestamp_us = 1000000};
    accel_sample_t below = {.y = 5999, .timestamp_us = 1050000};
    accel_sample_t second_crossing = {.y = 6000, .timestamp_us = 1100000};

    impact_init(&config);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                      impact_classify_threshold_crossing(&first_crossing, first_crossing.timestamp_us, &event));
    impact_mark_accepted(first_crossing.timestamp_us);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_BELOW_THRESHOLD,
                      impact_classify_threshold_crossing(&below, below.timestamp_us, &event));
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_REFRACTORY,
                      impact_classify_threshold_crossing(&second_crossing, second_crossing.timestamp_us, &event));
}

void test_impact_threshold_crossing_detects_negative_saturation_after_rearming(void) {
    impact_config_t config = {
        .axis = APP_IMPACT_AXIS_Y,
        .threshold_raw = 6000,
        .refractory_ms = 200,
    };
    impact_event_t event;
    accel_sample_t below = {.y = 5999, .timestamp_us = 1000000};
    accel_sample_t negative_saturation = {.y = INT16_MIN, .timestamp_us = 1001000};

    impact_init(&config);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_BELOW_THRESHOLD,
                      impact_classify_threshold_crossing(&below, below.timestamp_us, &event));
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                      impact_classify_threshold_crossing(&negative_saturation, negative_saturation.timestamp_us, &event));
    TEST_ASSERT_EQUAL_UINT32(32768U, event.axis_abs_raw);
}

void test_audio_impact_duration_is_available_for_calibration_timing(void) {
    audio_init();

    TEST_ASSERT_GREATER_THAN_UINT32(0, audio_impact_duration_us());
}

void test_audio_stop_cancels_active_pcm_playback(void) {
    audio_init();
    TEST_ASSERT_TRUE(audio_start_silence(1000U));
    TEST_ASSERT_TRUE(audio_is_active());

    audio_stop();

    TEST_ASSERT_FALSE(audio_is_active());
}

void test_audio_busy_rejects_a_candidate_after_its_refractory_period_until_playback_ends(void) {
    int64_t audio_busy_until_us = 0;

    TEST_ASSERT_TRUE(impact_claim_candidate_for_audio(
        IMPACT_CANDIDATE_PROCESSED, 1000000, 500000, &audio_busy_until_us));
    TEST_ASSERT_EQUAL_INT32(1500000, audio_busy_until_us);
    TEST_ASSERT_FALSE(impact_claim_candidate_for_audio(
        IMPACT_CANDIDATE_PROCESSED, 1300000, 500000, &audio_busy_until_us));
    TEST_ASSERT_TRUE(impact_claim_candidate_for_audio(
        IMPACT_CANDIDATE_PROCESSED, 1500000, 500000, &audio_busy_until_us));
}

void test_calibration_metadata_converts_the_impact_threshold_to_the_physical_acceleration(void) {
    float threshold_g = (float)APP_IMPACT_THRESHOLD_RAW * APP_ACCEL_SENSITIVITY_MG_LSB / 1000.0f;

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.928f, threshold_g);
}

void test_bluetooth_policy_requests_the_initial_connection_only_once(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, 0));
}

void test_bluetooth_policy_stops_requesting_after_a_connection_callback(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    bluetooth_audio_policy_on_connected(&policy);

    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, 10000000));
}

void test_bluetooth_policy_reconnects_at_the_exact_delay_boundary_after_disconnect(void) {
    bt_audio_policy_t policy;
    const int64_t disconnected_at_us = 1234567;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    bluetooth_audio_policy_on_disconnected(&policy, disconnected_at_us);

    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, disconnected_at_us + 9999999));
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, disconnected_at_us + 10000000));
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, disconnected_at_us + 10000001));
}

void test_calibration_playback_mapping_marks_only_busy_results_as_audio_busy_skipped(void) {
    calibration_playback_outcome_t started = calibration_playback_map_result(BT_AUDIO_PLAY_STARTED);
    calibration_playback_outcome_t busy = calibration_playback_map_result(BT_AUDIO_PLAY_BUSY);
    calibration_playback_outcome_t disconnected = calibration_playback_map_result(BT_AUDIO_PLAY_DISCONNECTED);
    calibration_playback_outcome_t error = calibration_playback_map_result(BT_AUDIO_PLAY_ERROR);

    TEST_ASSERT_EQUAL(CAL_IMPACT_PROCESSED, started.outcome);
    TEST_ASSERT_FALSE(started.audio_busy_skipped);
    TEST_ASSERT_EQUAL(CAL_IMPACT_SKIPPED, busy.outcome);
    TEST_ASSERT_TRUE(busy.audio_busy_skipped);
    TEST_ASSERT_EQUAL(CAL_IMPACT_SKIPPED, disconnected.outcome);
    TEST_ASSERT_FALSE(disconnected.audio_busy_skipped);
    TEST_ASSERT_EQUAL(CAL_IMPACT_SKIPPED, error.outcome);
    TEST_ASSERT_FALSE(error.audio_busy_skipped);
}

void app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cobs_round_trip_preserves_zero_bytes);
    RUN_TEST(test_crc32_matches_iso_hdlc_reference_vector);
    RUN_TEST(test_impact_event_packet_round_trip_preserves_timestamp_and_outcome);
    RUN_TEST(test_impact_telemetry_packet_round_trip_preserves_all_counters);
    RUN_TEST(test_impact_classifier_distinguishes_refractory_skips_from_threshold_rejection);
    RUN_TEST(test_impact_classifier_accepts_the_initial_candidate_without_timestamp_underflow);
    RUN_TEST(test_impact_classifier_accepts_exact_threshold_and_signed_saturation);
    RUN_TEST(test_impact_threshold_crossing_emits_once_until_the_signal_drops_below_threshold);
    RUN_TEST(test_impact_threshold_crossing_preserves_refractory_skip_outcomes);
    RUN_TEST(test_impact_threshold_crossing_detects_negative_saturation_after_rearming);
    RUN_TEST(test_audio_impact_duration_is_available_for_calibration_timing);
    RUN_TEST(test_audio_stop_cancels_active_pcm_playback);
    RUN_TEST(test_audio_busy_rejects_a_candidate_after_its_refractory_period_until_playback_ends);
    RUN_TEST(test_calibration_metadata_converts_the_impact_threshold_to_the_physical_acceleration);
    RUN_TEST(test_bluetooth_policy_requests_the_initial_connection_only_once);
    RUN_TEST(test_bluetooth_policy_stops_requesting_after_a_connection_callback);
    RUN_TEST(test_bluetooth_policy_reconnects_at_the_exact_delay_boundary_after_disconnect);
    RUN_TEST(test_calibration_playback_mapping_marks_only_busy_results_as_audio_busy_skipped);
    UNITY_END();
}
