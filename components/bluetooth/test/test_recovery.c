/* Native adapter fault injection. Build/run with test_recovery.py. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
static unsigned profile_inits;
static esp_err_t profile_result = ESP_OK;
static bool profile_success_during_request;
static void profile_event(int state);
static esp_err_t retry_profile_init(void) {
    ++profile_inits;
    if (profile_success_during_request) profile_event(ESP_A2D_INIT_SUCCESS);
    return profile_result;
}
/* The SDK test header supplies an inline successful source_init; override only
   the adapter call so asynchronous profile failures can be exercised. */
#define esp_a2d_source_init retry_profile_init
#include "../bluetooth_audio.c"
#undef esp_a2d_source_init

const uint8_t impact_pcm_start[4] = {0};
const uint8_t impact_pcm_end[1] = {0};
static unsigned connects, disconnects, media_commands;
static bool fail_disconnect, fail_media;
static int64_t now_us;
static bt_audio_state_t notified_state;
static void state_changed(bt_audio_state_t state) { notified_state = state; }
static void reset(void) {
    bluetooth_audio_policy_init(&s_policy);
    s_initialized = s_source_ready = s_started = true;
    s_worker = (void *)1;
    s_shutdown_requested = s_link_opening = s_link_connected = false;
    s_disconnect_pending = s_recovering = s_profile_retry_pending = false;
    s_profile_retry_at_us = 0;
    connects = disconnects = media_commands = profile_inits = 0;
    fail_disconnect = fail_media = false;
    profile_result = ESP_OK;
    profile_success_during_request = false;
    now_us = 0;
    s_state_callback = state_changed;
    notified_state = BT_AUDIO_DISABLED;
    audio_stop();
}
static void tick(int64_t time_us) { now_us = time_us; attempt_connection(now_us); }
static void emit_connection(esp_a2d_connection_state_t state) {
    esp_a2d_cb_param_t p = {0};
    p.conn_stat.state = state;
    memcpy(p.conn_stat.remote_bda, s_active_speaker_bda, ESP_BD_ADDR_LEN);
    a2dp_callback(ESP_A2D_CONNECTION_STATE_EVT, &p);
}
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                      UBaseType_t priority, TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *out = (void *)1; return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) { (void)clear; (void)ticks; return 0; }
int64_t esp_timer_get_time(void) { return now_us; }
EventGroupHandle_t xEventGroupCreate(void) { static struct { EventBits_t bits; } group; return (EventGroupHandle_t)&group; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits) { (void)g; return bits; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits, BaseType_t clear,
                              BaseType_t all, TickType_t ticks) {
    (void)g; (void)bits; (void)clear; (void)all; (void)ticks; return 0;
}
esp_err_t esp_a2d_source_connect(uint8_t *addr) {
    assert(!s_link_connected && !s_disconnect_pending);
    assert(memcmp(addr, s_active_speaker_bda, ESP_BD_ADDR_LEN) == 0);
    ++connects; return ESP_OK;
}
esp_err_t esp_a2d_source_disconnect(uint8_t *addr) {
    assert(s_link_connected && !s_link_opening);
    assert(memcmp(addr, s_active_speaker_bda, ESP_BD_ADDR_LEN) == 0);
    ++disconnects; return fail_disconnect ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_a2d_media_ctrl(esp_a2d_media_ctrl_t cmd) {
    (void)cmd; ++media_commands; return fail_media ? ESP_FAIL : ESP_OK;
}

static void profile_event(int state) {
    esp_a2d_cb_param_t p = {0};
    p.a2d_prof_stat.init_state = state;
    a2dp_callback(ESP_A2D_PROF_STATE_EVT, &p);
}

int main(void) {
    const int64_t delay = (int64_t)APP_BT_RECONNECT_MS * 1000;
    reset();
    tick(0);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    fail_disconnect = true;
    bluetooth_audio_request_reconnect();
    assert(!s_policy.reconnect_requested);
    assert(!bluetooth_audio_is_connected());
    tick(delay - 1);
    assert(disconnects == 1 && connects == 1);
    tick(delay);
    assert(disconnects == 2 && connects == 1);
    fail_disconnect = false;
    tick(2 * delay - 1);
    assert(disconnects == 2);
    tick(2 * delay);
    assert(disconnects == 3 && connects == 1);
    tick(3 * delay);
    assert(disconnects == 3); /* a successful request waits for its event */
    emit_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    tick(3 * delay);
    assert(connects == 2);
    assert(bluetooth_audio_policy_attempted_speaker(&s_policy) == BT_AUDIO_SPEAKER_SECONDARY);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    assert(bluetooth_audio_is_connected());
    assert(!s_policy.reconnect_requested);

    reset();
    s_source_ready = false;
    profile_inits = 0;
    profile_event(ESP_A2D_INIT_SUCCESS + 1); /* unexpected profile status */
    tick(delay - 1);
    assert(profile_inits == 0 && connects == 0);
    profile_result = ESP_FAIL;
    tick(delay);
    assert(profile_inits == 1 && connects == 0);
    profile_result = ESP_OK;
    tick(2 * delay - 1);
    assert(profile_inits == 1);
    tick(2 * delay);
    assert(profile_inits == 2 && connects == 0);
    tick(3 * delay - 1);
    assert(profile_inits == 2); /* allow time for asynchronous completion */
    tick(3 * delay);
    assert(profile_inits == 3); /* accepted init silently failed: retry */
    profile_event(ESP_A2D_INIT_SUCCESS);
    tick(3 * delay);
    assert(connects == 1);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    assert(bluetooth_audio_is_connected());

    reset();
    s_initialized = s_source_ready = false;
    assert(bluetooth_audio_init(state_changed));
    assert(profile_inits == 1);
    tick(delay - 1);
    assert(profile_inits == 1 && connects == 0);
    tick(delay);
    assert(profile_inits == 2 && connects == 0);
    profile_event(ESP_A2D_INIT_SUCCESS);
    tick(delay);
    assert(connects == 1);

    reset();
    s_source_ready = false;
    profile_event(ESP_A2D_INIT_SUCCESS + 1);
    profile_success_during_request = true;
    profile_result = ESP_ERR_INVALID_STATE;
    tick(delay);
    assert(s_source_ready && !s_profile_retry_pending);
    tick(delay);
    assert(connects == 1); /* concurrent success wins over redundant init */

    reset();
    tick(0);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    process_keep_media_started();
    esp_a2d_cb_param_t ack = {0};
    ack.media_ctrl_stat.cmd = ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY;
    ack.media_ctrl_stat.status = 1;
    a2dp_callback(ESP_A2D_MEDIA_CTRL_ACK_EVT, &ack);
    assert(!bluetooth_audio_is_connected());
    assert(!s_policy.source_ready);
    tick(delay - 1);
    assert(disconnects == 0);
    tick(delay);
    assert(disconnects == 1);
    emit_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    tick(delay);
    assert(connects == 2);

    reset();
    tick(0);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    fail_media = true;
    process_keep_media_started();
    assert(!bluetooth_audio_is_connected());
    process_keep_media_started();
    assert(media_commands == 1); /* no tight media-command retry loop */
    tick(delay - 1);
    assert(disconnects == 0);
    tick(delay);
    assert(disconnects == 1);

    reset();
    tick(0);
    bluetooth_audio_policy_on_recoverable_error(&s_policy, now_us);
    s_recovering = true;
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED); /* late opening completion */
    assert(!bluetooth_audio_is_connected());
    tick(delay - 1);
    assert(disconnects == 0);
    tick(delay);
    assert(disconnects == 1 && connects == 1);

    reset();
    tick(0);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    fail_disconnect = true;
    bluetooth_audio_request_reconnect();
    assert(!bluetooth_audio_shutdown(0));
    tick(10 * delay);
    process_keep_media_started();
    assert(connects == 1 && media_commands == 0);

    reset();
    s_policy.state = BT_AUDIO_ERROR;
    notified_state = BT_AUDIO_ERROR;
    bluetooth_audio_request_reconnect();
    assert(notified_state == BT_AUDIO_ERROR);
    profile_event(ESP_A2D_INIT_SUCCESS + 1);
    profile_event(ESP_A2D_INIT_SUCCESS);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    emit_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    tick(INT64_MAX);
    assert(s_policy.state == BT_AUDIO_ERROR && connects == 0);
    puts("PASS: failed disconnect/profile/media recovery and fatal error isolation");
    return 0;
}
