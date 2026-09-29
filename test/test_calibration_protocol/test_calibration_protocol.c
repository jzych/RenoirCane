#include <limits.h>
#include <string.h>

#include "app_config.h"
#include "audio.h"
#include "calibration_playback.h"
#include "bluetooth_audio_policy.h"
#include "calibration_protocol.h"
#include "impact.h"
#include "unity.h"

#define RECONNECT_DELAY_US ((int64_t)APP_BT_RECONNECT_MS * 1000)
#define PCM_BYTES_PER_SECOND ((size_t)APP_AUDIO_SAMPLE_RATE * APP_AUDIO_CHANNELS * 2U)

extern const uint8_t impact_pcm_start[];

/* Builds header + payload + CRC with the given fields and COBS-encodes it (no trailing delimiter). */
static size_t encode_raw_packet(uint8_t version, uint16_t declared_length, const uint8_t *payload, size_t payload_length,
                                uint32_t crc_xor, uint8_t *encoded, size_t encoded_capacity) {
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    raw[0] = version;
    raw[1] = CAL_PKT_METADATA;
    raw[2] = (uint8_t)declared_length;
    raw[3] = (uint8_t)(declared_length >> 8);
    memcpy(&raw[4], payload, payload_length);
    uint32_t crc = calibration_crc32(raw, 4U + payload_length) ^ crc_xor;
    for (size_t index = 0; index < 4U; ++index) raw[4U + payload_length + index] = (uint8_t)(crc >> (8U * index));
    return calibration_cobs_encode(raw, 8U + payload_length, encoded, encoded_capacity);
}

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

void test_packet_decode_accepts_a_well_formed_packet_built_by_the_test_helper(void) {
    const uint8_t payload[] = {0xAA, 0x00, 0xBB};
    uint8_t encoded[32];
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    calibration_packet_view_t packet;

    size_t length = encode_raw_packet(CALIBRATION_PROTOCOL_VERSION, sizeof(payload), payload, sizeof(payload), 0U,
                                      encoded, sizeof(encoded));

    TEST_ASSERT_TRUE(calibration_packet_decode(encoded, length, raw, sizeof(raw), &packet));
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), packet.payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, packet.payload, sizeof(payload));
}

void test_packet_decode_rejects_a_crc_mismatch(void) {
    const uint8_t payload[] = {0xAA, 0x00, 0xBB};
    uint8_t encoded[32];
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    calibration_packet_view_t packet;

    size_t length = encode_raw_packet(CALIBRATION_PROTOCOL_VERSION, sizeof(payload), payload, sizeof(payload), 1U,
                                      encoded, sizeof(encoded));

    TEST_ASSERT_FALSE(calibration_packet_decode(encoded, length, raw, sizeof(raw), &packet));
}

void test_packet_decode_rejects_an_unknown_protocol_version(void) {
    const uint8_t payload[] = {0xAA};
    uint8_t encoded[32];
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    calibration_packet_view_t packet;

    size_t length = encode_raw_packet(CALIBRATION_PROTOCOL_VERSION + 1U, sizeof(payload), payload, sizeof(payload), 0U,
                                      encoded, sizeof(encoded));

    TEST_ASSERT_FALSE(calibration_packet_decode(encoded, length, raw, sizeof(raw), &packet));
}

void test_packet_decode_rejects_a_declared_length_that_does_not_match_the_frame(void) {
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    uint8_t encoded[32];
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    calibration_packet_view_t packet;

    size_t length = encode_raw_packet(CALIBRATION_PROTOCOL_VERSION, sizeof(payload) + 1U, payload, sizeof(payload), 0U,
                                      encoded, sizeof(encoded));

    TEST_ASSERT_FALSE(calibration_packet_decode(encoded, length, raw, sizeof(raw), &packet));
}

void test_packet_decode_rejects_short_frames_invalid_cobs_and_small_scratch_buffers(void) {
    const uint8_t short_frame[] = {0x04, 0x01, 0x01, 0x01}; /* decodes to 3 bytes: less than header + CRC */
    const uint8_t truncated_cobs[] = {0x05, 0x01};         /* code claims 4 data bytes, 1 present */
    const uint8_t payload[] = {0xAA, 0xBB};
    uint8_t encoded[32];
    uint8_t raw[CALIBRATION_MAX_RAW_PACKET];
    calibration_packet_view_t packet;

    TEST_ASSERT_FALSE(calibration_packet_decode(short_frame, sizeof(short_frame), raw, sizeof(raw), &packet));
    TEST_ASSERT_FALSE(calibration_packet_decode(truncated_cobs, sizeof(truncated_cobs), raw, sizeof(raw), &packet));

    size_t length = encode_raw_packet(CALIBRATION_PROTOCOL_VERSION, sizeof(payload), payload, sizeof(payload), 0U,
                                      encoded, sizeof(encoded));
    TEST_ASSERT_TRUE(calibration_packet_decode(encoded, length, raw, 8U + sizeof(payload), &packet));
    TEST_ASSERT_FALSE(calibration_packet_decode(encoded, length, raw, 8U + sizeof(payload) - 1U, &packet));
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
    accel_sample_t positive_crossing = {.y = 6000, .timestamp_us = 1000000};
    accel_sample_t below = {.y = 5999, .timestamp_us = 1100000};
    accel_sample_t negative_saturation = {.y = INT16_MIN, .timestamp_us = 1300000};

    impact_init(&config);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                      impact_classify_threshold_crossing(&positive_crossing, positive_crossing.timestamp_us, &event));
    impact_mark_accepted(positive_crossing.timestamp_us);
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_BELOW_THRESHOLD,
                      impact_classify_threshold_crossing(&below, below.timestamp_us, &event));
    TEST_ASSERT_EQUAL(IMPACT_CANDIDATE_PROCESSED,
                      impact_classify_threshold_crossing(&negative_saturation, negative_saturation.timestamp_us, &event));
    TEST_ASSERT_EQUAL_UINT32(32768U, event.axis_abs_raw);
}

void test_audio_impact_duration_is_exactly_one_second_for_calibration_timing(void) {
    audio_init();

    TEST_ASSERT_EQUAL_UINT32(1000000U, audio_impact_duration_us());
}

void test_audio_stop_cancels_active_pcm_playback(void) {
    audio_init();
    TEST_ASSERT_TRUE(audio_start_silence(1000U));
    TEST_ASSERT_TRUE(audio_is_active());

    audio_stop();

    uint8_t buffer[16];
    memset(buffer, 0xA5, sizeof(buffer));
    TEST_ASSERT_FALSE(audio_is_active());
    TEST_ASSERT_EQUAL_INT32(0, audio_fill_pcm(buffer, sizeof(buffer)));
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, buffer, sizeof(buffer));
}

void test_audio_impact_finishes_after_one_second_of_pcm_without_transport_preroll(void) {
    const size_t asset_bytes = PCM_BYTES_PER_SECOND;
    uint8_t buffer[512];
    size_t supplied = 0;

    audio_init();
    TEST_ASSERT_TRUE(audio_start_impact());

    /* Every byte handed to A2DP is the cue itself, starting at its first byte: no silence ahead of it. */
    while (supplied < asset_bytes) {
        int32_t requested = (int32_t)((asset_bytes - supplied) < sizeof(buffer)
                                          ? (asset_bytes - supplied)
                                          : sizeof(buffer));
        TEST_ASSERT_EQUAL_INT32(requested, audio_fill_pcm(buffer, requested));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(&impact_pcm_start[supplied], buffer, (size_t)requested);
        supplied += (size_t)requested;
    }

    TEST_ASSERT_FALSE(audio_is_active());
    memset(buffer, 0xA5, sizeof(buffer));
    TEST_ASSERT_EQUAL_INT32(0, audio_fill_pcm(buffer, sizeof(buffer)));
    TEST_ASSERT_EACH_EQUAL_UINT8(0U, buffer, sizeof(buffer));
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

void test_impact_threshold_is_the_tuned_2_196_g_inside_the_sensor_range(void) {
    float threshold_g = (float)APP_IMPACT_THRESHOLD_RAW * APP_ACCEL_SENSITIVITY_MG_LSB / 1000.0f;

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.196f, threshold_g);
    TEST_ASSERT_TRUE(threshold_g < (float)APP_ACCEL_RANGE_G);
}

void test_bluetooth_policy_requests_the_initial_connection_only_once(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_PRIMARY, bluetooth_audio_policy_attempted_speaker(&policy));
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, 0));
}

void test_bluetooth_policy_stops_requesting_after_a_connection_callback(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    bluetooth_audio_policy_on_connected(&policy);

    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, 10000000));
}

void test_bluetooth_policy_keeps_media_started_while_connected(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_keep_media_started(&policy));

    bluetooth_audio_policy_on_connected(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_keep_media_started(&policy));

    bluetooth_audio_policy_on_disconnected(&policy, 1000000);
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_keep_media_started(&policy));
}

void test_bluetooth_policy_checks_source_readiness_before_starting_media(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    bluetooth_audio_policy_on_connected(&policy);

    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY,
                      bluetooth_audio_policy_next_media_action(&policy));
    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_NONE, bluetooth_audio_policy_next_media_action(&policy));

    bluetooth_audio_policy_on_source_ready(&policy, true);
    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_START, bluetooth_audio_policy_next_media_action(&policy));

    bluetooth_audio_policy_on_media_start_result(&policy, true);
    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_NONE, bluetooth_audio_policy_next_media_action(&policy));
}

void test_bluetooth_policy_retries_readiness_after_a_not_ready_response(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    bluetooth_audio_policy_on_connected(&policy);

    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY,
                      bluetooth_audio_policy_next_media_action(&policy));
    bluetooth_audio_policy_on_source_ready(&policy, false);

    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY,
                      bluetooth_audio_policy_next_media_action(&policy));
    TEST_ASSERT_EQUAL(BT_AUDIO_CONNECTED, policy.state);
}

void test_bluetooth_policy_restarts_media_after_remote_suspend(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    bluetooth_audio_policy_on_connected(&policy);
    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY,
                      bluetooth_audio_policy_next_media_action(&policy));
    bluetooth_audio_policy_on_source_ready(&policy, true);
    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_START, bluetooth_audio_policy_next_media_action(&policy));
    bluetooth_audio_policy_on_media_start_result(&policy, true);

    bluetooth_audio_policy_on_media_suspended(&policy);

    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY,
                      bluetooth_audio_policy_next_media_action(&policy));
}

void test_bluetooth_policy_ignores_a_late_start_ack_after_disconnect(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    bluetooth_audio_policy_on_connected(&policy);
    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY,
                      bluetooth_audio_policy_next_media_action(&policy));
    bluetooth_audio_policy_on_source_ready(&policy, true);
    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_START, bluetooth_audio_policy_next_media_action(&policy));

    bluetooth_audio_policy_on_disconnected(&policy, 1000000);

    TEST_ASSERT_FALSE(bluetooth_audio_policy_on_media_start_result(&policy, true));
    TEST_ASSERT_EQUAL(BT_AUDIO_DISABLED, policy.state);
    TEST_ASSERT_EQUAL(BT_AUDIO_MEDIA_ACTION_NONE, bluetooth_audio_policy_next_media_action(&policy));
}

void test_bluetooth_policy_reconnects_at_the_exact_delay_boundary_after_disconnect(void) {
    bt_audio_policy_t policy;
    const int64_t disconnected_at_us = 1234567;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    bluetooth_audio_policy_on_disconnected(&policy, disconnected_at_us);

    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, disconnected_at_us + RECONNECT_DELAY_US - 1));
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, disconnected_at_us + RECONNECT_DELAY_US));
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, disconnected_at_us + RECONNECT_DELAY_US + 1));
}

void test_bluetooth_policy_alternates_the_attempted_speaker_after_each_connection_request(void) {
    bt_audio_policy_t policy;
    const int64_t first_disconnect_us = 1000000;
    const int64_t second_disconnect_us = 7000000;

    bluetooth_audio_policy_init(&policy);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_PRIMARY, bluetooth_audio_policy_attempted_speaker(&policy));
    bluetooth_audio_policy_on_disconnected(&policy, first_disconnect_us);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, first_disconnect_us + RECONNECT_DELAY_US));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_SECONDARY, bluetooth_audio_policy_attempted_speaker(&policy));
    bluetooth_audio_policy_on_disconnected(&policy, second_disconnect_us);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, second_disconnect_us + RECONNECT_DELAY_US));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_PRIMARY, bluetooth_audio_policy_attempted_speaker(&policy));
}

void test_bluetooth_policy_retries_the_same_speaker_after_an_immediate_connection_failure(void) {
    bt_audio_policy_t policy;
    const int64_t failure_us = 1000000;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_PRIMARY, bluetooth_audio_policy_attempted_speaker(&policy));

    bluetooth_audio_policy_on_connection_request_failed(&policy, failure_us);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, failure_us + RECONNECT_DELAY_US));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_PRIMARY, bluetooth_audio_policy_attempted_speaker(&policy));
}

void test_bluetooth_policy_retries_immediately_after_a_manual_reconnect_during_connecting_fails(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));

    TEST_ASSERT_FALSE(bluetooth_audio_policy_request_reconnect(&policy));
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, 1));

    bluetooth_audio_policy_on_disconnected(&policy, 2);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 2));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_PRIMARY, bluetooth_audio_policy_attempted_speaker(&policy));
}

void test_bluetooth_policy_defers_manual_reconnect_until_the_connected_speaker_disconnects(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));
    bluetooth_audio_policy_on_connected(&policy);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_request_reconnect(&policy));
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, 1));

    bluetooth_audio_policy_on_disconnected(&policy, 2);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 2));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_PRIMARY, bluetooth_audio_policy_attempted_speaker(&policy));
}

void test_bluetooth_policy_requests_disconnect_when_a_pending_connect_completes(void) {
    bt_audio_policy_t policy;

    bluetooth_audio_policy_init(&policy);
    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 0));

    /* Disconnecting while the A2DP connection is opening is unreliable.  Retain the request until
       the connected event, then ask the caller to issue the disconnect on the established link. */
    TEST_ASSERT_FALSE(bluetooth_audio_policy_request_reconnect(&policy));
    TEST_ASSERT_TRUE(bluetooth_audio_policy_on_connected(&policy));
    TEST_ASSERT_FALSE(bluetooth_audio_policy_should_connect(&policy, 1));

    bluetooth_audio_policy_on_disconnected(&policy, 2);

    TEST_ASSERT_TRUE(bluetooth_audio_policy_should_connect(&policy, 2));
    TEST_ASSERT_EQUAL(BT_AUDIO_SPEAKER_PRIMARY, bluetooth_audio_policy_attempted_speaker(&policy));
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
    RUN_TEST(test_packet_decode_accepts_a_well_formed_packet_built_by_the_test_helper);
    RUN_TEST(test_packet_decode_rejects_a_crc_mismatch);
    RUN_TEST(test_packet_decode_rejects_an_unknown_protocol_version);
    RUN_TEST(test_packet_decode_rejects_a_declared_length_that_does_not_match_the_frame);
    RUN_TEST(test_packet_decode_rejects_short_frames_invalid_cobs_and_small_scratch_buffers);
    RUN_TEST(test_impact_telemetry_packet_round_trip_preserves_all_counters);
    RUN_TEST(test_impact_classifier_distinguishes_refractory_skips_from_threshold_rejection);
    RUN_TEST(test_impact_classifier_accepts_the_initial_candidate_without_timestamp_underflow);
    RUN_TEST(test_impact_classifier_accepts_exact_threshold_and_signed_saturation);
    RUN_TEST(test_impact_threshold_crossing_emits_once_until_the_signal_drops_below_threshold);
    RUN_TEST(test_impact_threshold_crossing_preserves_refractory_skip_outcomes);
    RUN_TEST(test_impact_threshold_crossing_detects_negative_saturation_after_rearming);
    RUN_TEST(test_audio_impact_duration_is_exactly_one_second_for_calibration_timing);
    RUN_TEST(test_audio_stop_cancels_active_pcm_playback);
    RUN_TEST(test_audio_impact_finishes_after_one_second_of_pcm_without_transport_preroll);
    RUN_TEST(test_audio_busy_rejects_a_candidate_after_its_refractory_period_until_playback_ends);
    RUN_TEST(test_impact_threshold_is_the_tuned_2_196_g_inside_the_sensor_range);
    RUN_TEST(test_bluetooth_policy_requests_the_initial_connection_only_once);
    RUN_TEST(test_bluetooth_policy_stops_requesting_after_a_connection_callback);
    RUN_TEST(test_bluetooth_policy_keeps_media_started_while_connected);
    RUN_TEST(test_bluetooth_policy_checks_source_readiness_before_starting_media);
    RUN_TEST(test_bluetooth_policy_retries_readiness_after_a_not_ready_response);
    RUN_TEST(test_bluetooth_policy_restarts_media_after_remote_suspend);
    RUN_TEST(test_bluetooth_policy_ignores_a_late_start_ack_after_disconnect);
    RUN_TEST(test_bluetooth_policy_reconnects_at_the_exact_delay_boundary_after_disconnect);
    RUN_TEST(test_bluetooth_policy_alternates_the_attempted_speaker_after_each_connection_request);
    RUN_TEST(test_bluetooth_policy_retries_the_same_speaker_after_an_immediate_connection_failure);
    RUN_TEST(test_bluetooth_policy_retries_immediately_after_a_manual_reconnect_during_connecting_fails);
    RUN_TEST(test_bluetooth_policy_defers_manual_reconnect_until_the_connected_speaker_disconnects);
    RUN_TEST(test_bluetooth_policy_requests_disconnect_when_a_pending_connect_completes);
    RUN_TEST(test_calibration_playback_mapping_marks_only_busy_results_as_audio_busy_skipped);
    UNITY_END();
}
