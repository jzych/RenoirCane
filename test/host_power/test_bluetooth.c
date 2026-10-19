#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
/* Exercise the real adapter and policy; replace only ESP-IDF/FreeRTOS calls. */
#include "../../components/bluetooth/bluetooth_audio.c"

const uint8_t impact_pcm_start[4] = {0};
const uint8_t impact_pcm_end[1] = {0};
static struct { EventBits_t bits; } group;
static unsigned connects, disconnects, media_commands;
static bool complete_disconnect, complete_open, fail_disconnect;
static TickType_t waited;
static void emit_connection(esp_a2d_connection_state_t state) {
    esp_a2d_cb_param_t p = {0};
    p.conn_stat.state = state;
    memcpy(p.conn_stat.remote_bda, s_active_speaker_bda, ESP_BD_ADDR_LEN);
    a2dp_callback(ESP_A2D_CONNECTION_STATE_EVT, &p);
}
static void reset(void) {
    bluetooth_audio_policy_init(&s_policy);
    s_initialized = true;
    s_source_ready = true;
    s_started = true;
    s_worker = (void *)1;
    s_shutdown_requested = false;
    s_link_opening = false;
    s_link_connected = false;
    s_disconnect_pending = false;
    s_shutdown_events = xEventGroupCreate();
    connects = disconnects = media_commands = 0;
    complete_disconnect = complete_open = fail_disconnect = false;
    waited = 0;
    audio_stop();
}
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                      UBaseType_t priority, TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; *out = (void *)1; return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) { (void)clear; (void)ticks; return 0; }
int64_t esp_timer_get_time(void) { return 0; }
EventGroupHandle_t xEventGroupCreate(void) { group.bits = 0; return (EventGroupHandle_t)&group; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits) { return g->bits |= bits; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits, BaseType_t clear,
                              BaseType_t all, TickType_t ticks) {
    (void)bits; (void)clear; (void)all; waited = ticks;
    process_shutdown();
    if (complete_open) emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    process_shutdown();
    if (complete_disconnect) emit_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    process_shutdown();
    return g->bits;
}
esp_err_t esp_a2d_source_connect(uint8_t *addr) {
    assert(memcmp(addr, s_active_speaker_bda, ESP_BD_ADDR_LEN) == 0);
    ++connects; return ESP_OK;
}
esp_err_t esp_a2d_source_disconnect(uint8_t *addr) {
    assert(memcmp(addr, s_active_speaker_bda, ESP_BD_ADDR_LEN) == 0);
    assert(s_link_connected); /* Opening links must never be disconnected early. */
    ++disconnects; return fail_disconnect ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_a2d_media_ctrl(esp_a2d_media_ctrl_t cmd) { (void)cmd; ++media_commands; return ESP_OK; }

int main(void) {
    reset();
    assert(audio_start_silence(100));
    assert(bluetooth_audio_shutdown(10000)); /* No connection: immediate completion. */
    assert(!audio_is_active());
    attempt_connection(10000000);
    assert(connects == 0);

    reset();
    attempt_connection(0);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    assert(audio_start_silence(100));
    complete_disconnect = true;
    assert(bluetooth_audio_shutdown(10000));
    assert(disconnects == 1 && connects == 1 && !audio_is_active());
    bluetooth_audio_request_reconnect();
    bluetooth_audio_start();
    attempt_connection(10000000);
    process_keep_media_started();
    assert(connects == 1 && media_commands == 0);
    assert(bluetooth_audio_start_impact() == BT_AUDIO_PLAY_DISCONNECTED);
    assert(!audio_is_active());

    reset();
    attempt_connection(0);
    assert(!bluetooth_audio_shutdown(0)); /* Opening remains pending. */
    assert(disconnects == 0);
    complete_open = complete_disconnect = true;
    assert(bluetooth_audio_shutdown(10000));
    assert(connects == 1 && disconnects == 1);

    reset();
    attempt_connection(0);
    assert(!bluetooth_audio_shutdown(10000));
    assert(waited == 10000 && disconnects == 0);
    emit_connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    process_shutdown();
    attempt_connection(10000000);
    assert(connects == 1);

    reset();
    attempt_connection(0);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    s_policy.state = BT_AUDIO_ERROR; /* Media errors can leave a physical link alive. */
    complete_disconnect = true;
    assert(bluetooth_audio_shutdown(10000));
    assert(disconnects == 1);

    reset();
    attempt_connection(0);
    emit_connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    fail_disconnect = true;
    assert(!bluetooth_audio_shutdown(10000));
    assert(disconnects == 1);
    puts("PASS: shutdown idle/connected/opening/timeout/error, no reconnect/media/playback");
    return 0;
}
