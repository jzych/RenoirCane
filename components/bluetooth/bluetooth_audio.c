#include "bluetooth_audio.h"

#include <string.h>

#include "app_config.h"
#include "audio.h"
#include "esp_a2dp_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define WORKER_INTERVAL_MS 50U
#define DISCONNECT_COMPLETION_TIMEOUT_MS 10000U
#define SHUTDOWN_COMPLETE_BIT (1U << 0)

static const char *TAG = "BT_AUDIO";
static const esp_bd_addr_t s_speaker_bdas[] = {
    APP_BT_SPEAKER_PRIMARY_BDA,
    APP_BT_SPEAKER_SECONDARY_BDA,
};
static esp_bd_addr_t s_active_speaker_bda;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bt_audio_policy_t s_policy;
static bt_audio_state_callback_t s_state_callback;
static TaskHandle_t s_worker;
static bool s_initialized;
static bool s_source_ready;
static bool s_started;
static bool s_shutdown_requested;
/* Track the physical link independently of policy/media errors. */
static bool s_link_opening;
static bool s_link_connected;
static bool s_disconnect_pending;
static int64_t s_disconnect_deadline_us;
static bool s_profile_cleanup_pending;
static bool s_recovering;
static bool s_profile_retry_pending;
static int64_t s_profile_retry_at_us;
static EventGroupHandle_t s_shutdown_events;

static void wake_worker(void) {
    TaskHandle_t worker;
    portENTER_CRITICAL(&s_lock);
    worker = s_worker;
    portEXIT_CRITICAL(&s_lock);
    if (worker != NULL) {
        xTaskNotifyGive(worker);
    }
}

static bool is_target(const uint8_t *bda) {
    bool matches;
    portENTER_CRITICAL(&s_lock);
    matches = bda != NULL && memcmp(bda, s_active_speaker_bda, ESP_BD_ADDR_LEN) == 0;
    portEXIT_CRITICAL(&s_lock);
    return matches;
}

static void notify_state(bt_audio_state_t state) {
    bt_audio_state_callback_t callback;
    portENTER_CRITICAL(&s_lock);
    callback = s_shutdown_requested ? NULL : s_state_callback;
    portEXIT_CRITICAL(&s_lock);
    if (callback != NULL) {
        callback(state);
    }
}

static void set_state(bt_audio_state_t state) {
    bt_audio_state_callback_t callback = NULL;
    bool changed = false;

    portENTER_CRITICAL(&s_lock);
    if (s_policy.state != state) {
        s_policy.state = state;
        callback = s_shutdown_requested ? NULL : s_state_callback;
        changed = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (changed && callback != NULL) {
        callback(state);
    }
}

static void fatal_error(const char *operation, esp_err_t error) {
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
    audio_stop();
    set_state(BT_AUDIO_ERROR);
}

static void recoverable_error(const char *operation, esp_err_t error) {
    ESP_LOGE(TAG, "%s failed: %s; retrying after %u ms", operation, esp_err_to_name(error),
             APP_BT_RECONNECT_MS);
    audio_stop();
    portENTER_CRITICAL(&s_lock);
    bluetooth_audio_policy_on_recoverable_error(&s_policy, esp_timer_get_time());
    s_recovering = s_policy.state != BT_AUDIO_ERROR;
    bt_audio_state_t state = s_policy.state;
    portEXIT_CRITICAL(&s_lock);
    notify_state(state);
    wake_worker();
}

static void request_disconnect(void) {
    portENTER_CRITICAL(&s_lock);
    s_disconnect_deadline_us = esp_timer_get_time() + (int64_t)DISCONNECT_COMPLETION_TIMEOUT_MS * 1000;
    portEXIT_CRITICAL(&s_lock);
    esp_err_t error = esp_a2d_source_disconnect(s_active_speaker_bda);
    if (error != ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_disconnect_pending = false;
        portEXIT_CRITICAL(&s_lock);
        recoverable_error("A2DP disconnect request", error);
    }
}

static int32_t a2dp_data_callback(uint8_t *data, int32_t len) {
    if (len < 0) {
        audio_stop();
        return 0;
    }
    if (data == NULL || len == 0) {
        return 0;
    }

    (void)audio_fill_pcm(data, len);
    return len;
}

static void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    if (event == ESP_BT_GAP_CFM_REQ_EVT) {
        bool accept = is_target(param->cfm_req.bda);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, accept);
        ESP_LOGI(TAG, "SSP confirmation %s for configured speaker", accept ? "accepted" : "rejected");
    }
}

static void connection_state_changed(const esp_a2d_cb_param_t *param) {
    const uint8_t *remote = param->conn_stat.remote_bda;
    if (!is_target(remote)) {
        if (param->conn_stat.state != ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            ESP_LOGW(TAG, "rejecting A2DP connection from non-configured peer");
            (void)esp_a2d_source_disconnect((uint8_t *)remote);
        }
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        bool disconnect_for_reconnect;
        portENTER_CRITICAL(&s_lock);
        s_link_opening = false;
        s_link_connected = true;
        disconnect_for_reconnect = !s_recovering && bluetooth_audio_policy_on_connected(&s_policy);
        disconnect_for_reconnect = disconnect_for_reconnect && !s_shutdown_requested && !s_disconnect_pending;
        if (disconnect_for_reconnect) s_disconnect_pending = true;
        portEXIT_CRITICAL(&s_lock);
        notify_state(bluetooth_audio_get_state());
        if (disconnect_for_reconnect) {
            ESP_LOGI(TAG, "disconnecting established A2DP link for requested reconnect");
            request_disconnect();
        }
        wake_worker();
        ESP_LOGI(TAG, "connected to configured speaker");
    } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        audio_stop();
        portENTER_CRITICAL(&s_lock);
        s_link_opening = false;
        s_link_connected = false;
        s_disconnect_pending = false;
        /* Recovery already scheduled a deadline. A late disconnect must not
           postpone it or apply a stale manual-reconnect shortcut. */
        if (!s_recovering) bluetooth_audio_policy_on_disconnected(&s_policy, now_us);
        portEXIT_CRITICAL(&s_lock);
        notify_state(bluetooth_audio_get_state());
        wake_worker();
        ESP_LOGW(TAG, "configured speaker disconnected");
    }
}

static void media_acknowledged(const esp_a2d_cb_param_t *param) {
    esp_a2d_media_ctrl_t command = param->media_ctrl_stat.cmd;
    bool success = param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS;
    bool handled;
    if (command == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) {
        portENTER_CRITICAL(&s_lock);
        handled = bluetooth_audio_policy_on_source_ready(&s_policy, success);
        portEXIT_CRITICAL(&s_lock);
        if (handled) {
            if (success) wake_worker();
            else recoverable_error("A2DP source readiness", ESP_FAIL);
        }
    } else if (command == ESP_A2D_MEDIA_CTRL_START) {
        portENTER_CRITICAL(&s_lock);
        handled = bluetooth_audio_policy_on_media_start_result(&s_policy, success);
        portEXIT_CRITICAL(&s_lock);
        if (handled) {
            if (success) {
                set_state(BT_AUDIO_STREAMING);
                wake_worker();
            } else {
                recoverable_error("A2DP media start", ESP_FAIL);
            }
        }
    } else if (command == ESP_A2D_MEDIA_CTRL_SUSPEND) {
        portENTER_CRITICAL(&s_lock);
        handled = bluetooth_audio_policy_on_media_suspended(&s_policy);
        portEXIT_CRITICAL(&s_lock);
        if (handled) {
            set_state(BT_AUDIO_CONNECTED);
            wake_worker();
        }
    }
}

static void a2dp_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    if (param == NULL) {
        return;
    }
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            connection_state_changed(param);
            break;
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            media_acknowledged(param);
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
                bool handled;
                portENTER_CRITICAL(&s_lock);
                handled = bluetooth_audio_policy_on_media_start_result(&s_policy, true);
                portEXIT_CRITICAL(&s_lock);
                if (handled) {
                    set_state(BT_AUDIO_STREAMING);
                }
            } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_SUSPEND) {
                bool handled;
                portENTER_CRITICAL(&s_lock);
                handled = bluetooth_audio_policy_on_media_suspended(&s_policy);
                portEXIT_CRITICAL(&s_lock);
                if (handled) {
                    set_state(BT_AUDIO_CONNECTED);
                    wake_worker();
                }
            }
            break;
        case ESP_A2D_PROF_STATE_EVT:
            portENTER_CRITICAL(&s_lock);
            bool cleaning_up = s_profile_cleanup_pending;
            if (cleaning_up && param->a2d_prof_stat.init_state == ESP_A2D_DEINIT_SUCCESS) {
                /* Confirmed profile teardown invalidates its old link. A late
                   DISCONNECTED alone cannot finish a pending profile cleanup. */
                s_profile_cleanup_pending = false;
                s_link_opening = false;
                s_link_connected = false;
                s_disconnect_pending = false;
                s_source_ready = false;
                s_profile_retry_pending = true;
                s_profile_retry_at_us = esp_timer_get_time() + (int64_t)APP_BT_RECONNECT_MS * 1000;
                bluetooth_audio_policy_on_recoverable_error(&s_policy, esp_timer_get_time());
                s_recovering = s_policy.state != BT_AUDIO_ERROR;
            }
            portEXIT_CRITICAL(&s_lock);
            if (cleaning_up) {
                if (param->a2d_prof_stat.init_state == ESP_A2D_DEINIT_SUCCESS) {
                    audio_stop();
                    notify_state(bluetooth_audio_get_state());
                    wake_worker();
                    ESP_LOGI(TAG, "A2DP profile cleanup complete; waiting to initialize source");
                }
                break;
            }
            if (param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) {
                portENTER_CRITICAL(&s_lock);
                s_source_ready = true;
                s_profile_retry_pending = false;
                portEXIT_CRITICAL(&s_lock);
                wake_worker();
            } else {
                portENTER_CRITICAL(&s_lock);
                s_source_ready = false;
                s_profile_retry_pending = true;
                portEXIT_CRITICAL(&s_lock);
                recoverable_error("A2DP source profile initialization", ESP_FAIL);
            }
            break;
        default:
            break;
    }
}

static void attempt_connection(int64_t now_us) {
    bool should_connect = false;
    bool disconnect = false;
    bool retry_profile = false;
    bool cleanup_profile = false;
    bt_audio_speaker_t speaker = BT_AUDIO_SPEAKER_PRIMARY;
    portENTER_CRITICAL(&s_lock);
    if (!s_shutdown_requested && s_policy.state != BT_AUDIO_ERROR && now_us >= s_policy.retry_at_us) {
        if (s_profile_cleanup_pending || s_disconnect_pending) {
            if (now_us >= s_disconnect_deadline_us) {
                /* IDF may drop a queued close, then ignore further disconnects
                   in CLOSING. Reset only the A2DP profile through its public
                   cleanup API; never invent a disconnected link or reconnect
                   until profile teardown has actually been confirmed. */
                cleanup_profile = true;
                s_profile_cleanup_pending = true;
                s_source_ready = false;
                bluetooth_audio_policy_on_recoverable_error(&s_policy, now_us);
                s_recovering = true;
                s_disconnect_deadline_us = now_us + (int64_t)DISCONNECT_COMPLETION_TIMEOUT_MS * 1000;
            }
        } else if (s_recovering && s_link_opening) {
            /* The opening link must finish before it can be disconnected. */
        } else if (s_recovering && s_link_connected) {
            disconnect = true;
            s_disconnect_pending = true;
        } else if (s_profile_retry_pending && now_us >= s_profile_retry_at_us) {
            retry_profile = true;
            /* ESP_OK means queued, not initialized. Some IDF asynchronous
               failures emit no completion event; retain a bounded retry. */
            s_profile_retry_at_us = now_us + (int64_t)APP_BT_RECONNECT_MS * 1000;
        } else if (s_source_ready) {
            should_connect = bluetooth_audio_policy_should_connect(&s_policy, now_us);
        }
    }
    if (should_connect) {
        speaker = bluetooth_audio_policy_attempted_speaker(&s_policy);
        memcpy(s_active_speaker_bda, s_speaker_bdas[speaker], ESP_BD_ADDR_LEN);
        s_link_opening = true;
        s_recovering = false;
    }
    portEXIT_CRITICAL(&s_lock);
    if (cleanup_profile) {
        audio_stop();
        notify_state(bluetooth_audio_get_state());
        ESP_LOGW(TAG, "A2DP disconnect completion timed out; requesting profile cleanup");
        esp_err_t error = esp_a2d_source_deinit();
        if (error != ESP_OK) {
            portENTER_CRITICAL(&s_lock);
            if (s_profile_cleanup_pending) {
                s_disconnect_deadline_us = now_us + (int64_t)APP_BT_RECONNECT_MS * 1000;
            }
            portEXIT_CRITICAL(&s_lock);
            ESP_LOGW(TAG, "A2DP profile cleanup request failed: %s", esp_err_to_name(error));
        }
    }
    if (disconnect) request_disconnect();
    if (retry_profile) {
        ESP_LOGI(TAG, "retrying A2DP source profile initialization");
        esp_err_t error = esp_a2d_source_init();
        if (error != ESP_OK) {
            portENTER_CRITICAL(&s_lock);
            /* An older queued init may complete between choosing this retry
               and calling IDF. Preserve that success over INVALID_STATE. */
            bool retry_failed = !s_source_ready;
            portEXIT_CRITICAL(&s_lock);
            if (retry_failed) recoverable_error("A2DP source profile initialization", error);
        }
    }
    if (!should_connect) {
        return;
    }

    ESP_LOGI(TAG, "A2DP connect request to %s speaker",
             speaker == BT_AUDIO_SPEAKER_PRIMARY ? "primary" : "secondary");
    notify_state(BT_AUDIO_CONNECTING);
    esp_err_t error = esp_a2d_source_connect(s_active_speaker_bda);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "A2DP connection request failed immediately: %s", esp_err_to_name(error));
        portENTER_CRITICAL(&s_lock);
        s_link_opening = false;
        bluetooth_audio_policy_on_connection_request_failed(&s_policy, now_us);
        portEXIT_CRITICAL(&s_lock);
        notify_state(BT_AUDIO_DISABLED);
    }
}

static void process_keep_media_started(void) {
    bt_audio_media_action_t action;
    portENTER_CRITICAL(&s_lock);
    action = s_shutdown_requested ? BT_AUDIO_MEDIA_ACTION_NONE
                                  : bluetooth_audio_policy_next_media_action(&s_policy);
    portEXIT_CRITICAL(&s_lock);
    if (action == BT_AUDIO_MEDIA_ACTION_NONE) {
        return;
    }

    esp_a2d_media_ctrl_t command = action == BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY
                                        ? ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY
                                        : ESP_A2D_MEDIA_CTRL_START;
    esp_err_t error = esp_a2d_media_ctrl(command);
    if (error != ESP_OK) {
        recoverable_error("A2DP media command", error);
    }
}

static void process_shutdown(void) {
    portENTER_CRITICAL(&s_lock);
    bool complete = s_shutdown_requested && !s_link_opening && !s_link_connected;
    bool disconnect = s_shutdown_requested && s_link_connected && !s_disconnect_pending;
    if (disconnect) s_disconnect_pending = true;
    portEXIT_CRITICAL(&s_lock);

    if (disconnect) {
        ESP_LOGI(TAG, "disconnecting established A2DP link for standby");
        esp_err_t error = esp_a2d_source_disconnect(s_active_speaker_bda);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "standby disconnect request failed: %s", esp_err_to_name(error));
        }
    }
    if (complete && s_shutdown_events != NULL) {
        xEventGroupSetBits(s_shutdown_events, SHUTDOWN_COMPLETE_BIT);
    }
}

static void bluetooth_worker(void *arg) {
    (void)arg;
    for (;;) {
        int64_t now_us = esp_timer_get_time();
        attempt_connection(now_us);
        process_keep_media_started();
        process_shutdown();
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WORKER_INTERVAL_MS));
    }
}

bool bluetooth_audio_init(bt_audio_state_callback_t callback) {
    portENTER_CRITICAL(&s_lock);
    if (s_initialized) {
        s_state_callback = callback;
        portEXIT_CRITICAL(&s_lock);
        return true;
    }
    s_state_callback = callback;
    bluetooth_audio_policy_init(&s_policy);
    portEXIT_CRITICAL(&s_lock);

    if (s_shutdown_events == NULL) s_shutdown_events = xEventGroupCreate();
    if (s_shutdown_events == NULL) {
        fatal_error("shutdown event allocation", ESP_ERR_NO_MEM);
        return false;
    }

    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        fatal_error("NVS initialization", error);
        return false;
    }
    error = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (error != ESP_OK) {
        fatal_error("BLE controller memory release", error);
        return false;
    }
    esp_bt_controller_config_t controller_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((error = esp_bt_controller_init(&controller_config)) != ESP_OK ||
        (error = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        fatal_error("Classic Bluetooth controller initialization", error);
        return false;
    }
    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_config.ssp_en = true;
    if ((error = esp_bluedroid_init_with_cfg(&bluedroid_config)) != ESP_OK ||
        (error = esp_bluedroid_enable()) != ESP_OK) {
        fatal_error("Bluedroid initialization", error);
        return false;
    }

    uint8_t io_capability = ESP_BT_IO_CAP_NONE;
    if ((error = esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_capability, sizeof(io_capability))) != ESP_OK ||
        (error = esp_bt_gap_register_callback(gap_callback)) != ESP_OK ||
        (error = esp_bt_dev_set_device_name("Cane Audio ECU")) != ESP_OK ||
        (error = esp_a2d_register_callback(a2dp_callback)) != ESP_OK ||
        (error = esp_a2d_source_register_data_callback(a2dp_data_callback)) != ESP_OK) {
        fatal_error("Bluetooth profile initialization", error);
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    s_profile_retry_pending = true;
    s_profile_retry_at_us = esp_timer_get_time() + (int64_t)APP_BT_RECONNECT_MS * 1000;
    portEXIT_CRITICAL(&s_lock);
    if ((error = esp_a2d_source_init()) != ESP_OK ||
        (error = esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE)) != ESP_OK) {
        fatal_error("Bluetooth profile initialization", error);
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    s_initialized = true;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

void bluetooth_audio_start(void) {
    portENTER_CRITICAL(&s_lock);
    bool create_worker = s_initialized && !s_started && !s_shutdown_requested;
    if (create_worker) {
        s_started = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (create_worker && xTaskCreate(bluetooth_worker, "bt_audio", 4096U, NULL, 5U, &s_worker) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_started = false;
        portEXIT_CRITICAL(&s_lock);
        fatal_error("Bluetooth worker creation", ESP_ERR_NO_MEM);
    }
}

bool bluetooth_audio_shutdown(uint32_t timeout_ms) {
    portENTER_CRITICAL(&s_lock);
    s_shutdown_requested = true;
    bool complete = !s_link_opening && !s_link_connected;
    portEXIT_CRITICAL(&s_lock);
    /* start_impact tests the shutdown gate and starts PCM under the same lock,
       so an impact cannot start playback after this stop. */
    audio_stop();
    if (complete) return true;

    wake_worker();
    EventBits_t bits = xEventGroupWaitBits(s_shutdown_events, SHUTDOWN_COMPLETE_BIT,
                                          pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    bool disconnected = (bits & SHUTDOWN_COMPLETE_BIT) != 0;
    if (!disconnected) ESP_LOGW(TAG, "standby disconnect timed out after %lu ms", (unsigned long)timeout_ms);
    return disconnected;
}

bt_audio_play_result_t bluetooth_audio_start_impact(void) {
    portENTER_CRITICAL(&s_lock);
    bt_audio_state_t state = s_policy.state;

    if (s_shutdown_requested || (state != BT_AUDIO_CONNECTED && state != BT_AUDIO_STREAMING)) {
        portEXIT_CRITICAL(&s_lock);
        return BT_AUDIO_PLAY_DISCONNECTED;
    }
    bool started = audio_start_impact();
    portEXIT_CRITICAL(&s_lock);
    if (!started) return BT_AUDIO_PLAY_BUSY;

    wake_worker();
    return BT_AUDIO_PLAY_STARTED;
}

void bluetooth_audio_request_reconnect(void) {
    bool disconnect;
    bool awaiting_connection;
    portENTER_CRITICAL(&s_lock);
    if (s_shutdown_requested || s_policy.state == BT_AUDIO_ERROR || s_recovering) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    awaiting_connection = s_policy.state == BT_AUDIO_CONNECTING;
    disconnect = bluetooth_audio_policy_request_reconnect(&s_policy);
    disconnect = disconnect && !s_disconnect_pending;
    if (disconnect) s_disconnect_pending = true;
    portEXIT_CRITICAL(&s_lock);

    audio_stop();
    if (disconnect) {
        request_disconnect();
    } else if (!awaiting_connection) {
        notify_state(bluetooth_audio_get_state());
        wake_worker();
    }
}

bool bluetooth_audio_is_connected(void) {
    bt_audio_state_t state = bluetooth_audio_get_state();
    return state == BT_AUDIO_CONNECTED || state == BT_AUDIO_STREAMING;
}

bt_audio_state_t bluetooth_audio_get_state(void) {
    portENTER_CRITICAL(&s_lock);
    bt_audio_state_t state = s_policy.state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}
