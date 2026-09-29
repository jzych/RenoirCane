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
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define WORKER_INTERVAL_MS 50U

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
    callback = s_state_callback;
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
        callback = s_state_callback;
        changed = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (changed && callback != NULL) {
        callback(state);
    }
}

static void media_error(const char *operation, esp_err_t error) {
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
    audio_stop();
    set_state(BT_AUDIO_ERROR);
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
        disconnect_for_reconnect = bluetooth_audio_policy_on_connected(&s_policy);
        portEXIT_CRITICAL(&s_lock);
        notify_state(BT_AUDIO_CONNECTED);
        if (disconnect_for_reconnect) {
            ESP_LOGI(TAG, "disconnecting established A2DP link for requested reconnect");
            esp_err_t error = esp_a2d_source_disconnect(s_active_speaker_bda);
            if (error != ESP_OK) {
                media_error("A2DP disconnect request", error);
            }
        }
        wake_worker();
        ESP_LOGI(TAG, "connected to configured speaker");
    } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        audio_stop();
        portENTER_CRITICAL(&s_lock);
        bluetooth_audio_policy_on_disconnected(&s_policy, now_us);
        portEXIT_CRITICAL(&s_lock);
        notify_state(BT_AUDIO_DISABLED);
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
            wake_worker();
        }
    } else if (command == ESP_A2D_MEDIA_CTRL_START) {
        portENTER_CRITICAL(&s_lock);
        handled = bluetooth_audio_policy_on_media_start_result(&s_policy, success);
        portEXIT_CRITICAL(&s_lock);
        if (handled) {
            set_state(success ? BT_AUDIO_STREAMING : BT_AUDIO_CONNECTED);
            wake_worker();
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
            if (param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) {
                portENTER_CRITICAL(&s_lock);
                s_source_ready = true;
                portEXIT_CRITICAL(&s_lock);
                wake_worker();
            } else {
                media_error("A2DP source profile initialization", ESP_FAIL);
            }
            break;
        default:
            break;
    }
}

static void attempt_connection(int64_t now_us) {
    bool should_connect;
    bt_audio_speaker_t speaker = BT_AUDIO_SPEAKER_PRIMARY;
    portENTER_CRITICAL(&s_lock);
    should_connect = s_source_ready && s_policy.state != BT_AUDIO_ERROR &&
                     bluetooth_audio_policy_should_connect(&s_policy, now_us);
    if (should_connect) {
        speaker = bluetooth_audio_policy_attempted_speaker(&s_policy);
        memcpy(s_active_speaker_bda, s_speaker_bdas[speaker], ESP_BD_ADDR_LEN);
    }
    portEXIT_CRITICAL(&s_lock);
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
        bluetooth_audio_policy_on_connection_request_failed(&s_policy, now_us);
        portEXIT_CRITICAL(&s_lock);
        notify_state(BT_AUDIO_DISABLED);
    }
}

static void process_keep_media_started(void) {
    bt_audio_media_action_t action;
    portENTER_CRITICAL(&s_lock);
    action = bluetooth_audio_policy_next_media_action(&s_policy);
    portEXIT_CRITICAL(&s_lock);
    if (action == BT_AUDIO_MEDIA_ACTION_NONE) {
        return;
    }

    esp_a2d_media_ctrl_t command = action == BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY
                                        ? ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY
                                        : ESP_A2D_MEDIA_CTRL_START;
    esp_err_t error = esp_a2d_media_ctrl(command);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "A2DP media command %d failed: %s", command, esp_err_to_name(error));
        portENTER_CRITICAL(&s_lock);
        if (action == BT_AUDIO_MEDIA_ACTION_CHECK_SOURCE_READY) {
            bluetooth_audio_policy_on_source_ready(&s_policy, false);
        } else {
            bluetooth_audio_policy_on_media_start_result(&s_policy, false);
        }
        portEXIT_CRITICAL(&s_lock);
    }
}

static void bluetooth_worker(void *arg) {
    (void)arg;
    for (;;) {
        int64_t now_us = esp_timer_get_time();
        attempt_connection(now_us);
        process_keep_media_started();
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

    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        media_error("NVS initialization", error);
        return false;
    }
    error = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (error != ESP_OK) {
        media_error("BLE controller memory release", error);
        return false;
    }
    esp_bt_controller_config_t controller_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((error = esp_bt_controller_init(&controller_config)) != ESP_OK ||
        (error = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        media_error("Classic Bluetooth controller initialization", error);
        return false;
    }
    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_config.ssp_en = true;
    if ((error = esp_bluedroid_init_with_cfg(&bluedroid_config)) != ESP_OK ||
        (error = esp_bluedroid_enable()) != ESP_OK) {
        media_error("Bluedroid initialization", error);
        return false;
    }

    uint8_t io_capability = ESP_BT_IO_CAP_NONE;
    if ((error = esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_capability, sizeof(io_capability))) != ESP_OK ||
        (error = esp_bt_gap_register_callback(gap_callback)) != ESP_OK ||
        (error = esp_bt_dev_set_device_name("Cane Audio ECU")) != ESP_OK ||
        (error = esp_a2d_register_callback(a2dp_callback)) != ESP_OK ||
        (error = esp_a2d_source_register_data_callback(a2dp_data_callback)) != ESP_OK ||
        (error = esp_a2d_source_init()) != ESP_OK ||
        (error = esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE)) != ESP_OK) {
        media_error("Bluetooth profile initialization", error);
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    s_initialized = true;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

void bluetooth_audio_start(void) {
    portENTER_CRITICAL(&s_lock);
    bool create_worker = s_initialized && !s_started;
    if (create_worker) {
        s_started = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (create_worker && xTaskCreate(bluetooth_worker, "bt_audio", 4096U, NULL, 5U, &s_worker) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_started = false;
        portEXIT_CRITICAL(&s_lock);
        media_error("Bluetooth worker creation", ESP_ERR_NO_MEM);
    }
}

bt_audio_play_result_t bluetooth_audio_start_impact(void) {
    portENTER_CRITICAL(&s_lock);
    bt_audio_state_t state = s_policy.state;
    portEXIT_CRITICAL(&s_lock);

    if (state != BT_AUDIO_CONNECTED && state != BT_AUDIO_STREAMING) {
        return BT_AUDIO_PLAY_DISCONNECTED;
    }
    if (!audio_start_impact()) {
        return BT_AUDIO_PLAY_BUSY;
    }

    wake_worker();
    return BT_AUDIO_PLAY_STARTED;
}

void bluetooth_audio_request_reconnect(void) {
    bool disconnect;
    bool awaiting_connection;
    portENTER_CRITICAL(&s_lock);
    awaiting_connection = s_policy.state == BT_AUDIO_CONNECTING;
    disconnect = bluetooth_audio_policy_request_reconnect(&s_policy);
    portEXIT_CRITICAL(&s_lock);

    audio_stop();
    if (disconnect) {
        esp_err_t error = esp_a2d_source_disconnect(s_active_speaker_bda);
        if (error != ESP_OK) {
            media_error("A2DP disconnect request", error);
        }
    } else if (!awaiting_connection) {
        notify_state(BT_AUDIO_DISABLED);
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
