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

typedef enum {
    MEDIA_IDLE,
    MEDIA_STARTING,
    MEDIA_STREAMING,
    MEDIA_SUSPENDING,
} media_state_t;

typedef enum {
    PCM_NONE,
    PCM_IMPACT,
    PCM_KEEPALIVE,
} pcm_kind_t;

static const char *TAG = "BT_AUDIO";
static const esp_bd_addr_t s_speaker_bda = APP_BT_SPEAKER_BDA;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bt_audio_policy_t s_policy;
static bt_audio_state_callback_t s_state_callback;
static TaskHandle_t s_worker;
static media_state_t s_media_state;
static pcm_kind_t s_pcm_kind;
static int64_t s_idle_since_us;
static int64_t s_drain_at_us;
static bool s_initialized;
static bool s_source_ready;
static bool s_started;
static bool s_reconnect_requested;

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
    return bda != NULL && memcmp(bda, s_speaker_bda, ESP_BD_ADDR_LEN) == 0;
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
    portENTER_CRITICAL(&s_lock);
    s_media_state = MEDIA_IDLE;
    s_pcm_kind = PCM_NONE;
    s_drain_at_us = 0;
    portEXIT_CRITICAL(&s_lock);
    set_state(BT_AUDIO_ERROR);
}

static esp_err_t start_media(void) {
    portENTER_CRITICAL(&s_lock);
    if (s_media_state == MEDIA_STREAMING || s_media_state == MEDIA_STARTING) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    s_media_state = MEDIA_STARTING;
    portEXIT_CRITICAL(&s_lock);

    esp_err_t error = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    if (error != ESP_OK) {
        media_error("A2DP media start", error);
    }
    return error;
}

static int32_t a2dp_data_callback(uint8_t *data, int32_t len) {
    if (len < 0) {
        audio_stop();
        portENTER_CRITICAL(&s_lock);
        s_pcm_kind = PCM_NONE;
        portEXIT_CRITICAL(&s_lock);
        return 0;
    }
    if (data == NULL || len == 0) {
        return 0;
    }

    int32_t read = audio_fill_pcm(data, len);
    if (read < len) {
        portENTER_CRITICAL(&s_lock);
        if (s_drain_at_us == 0) {
            s_drain_at_us = esp_timer_get_time() + (int64_t)APP_A2DP_DRAIN_GRACE_MS * 1000;
        }
        portEXIT_CRITICAL(&s_lock);
        wake_worker();
    }
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
        portENTER_CRITICAL(&s_lock);
        bluetooth_audio_policy_on_connected(&s_policy);
        s_media_state = MEDIA_IDLE;
        s_pcm_kind = PCM_NONE;
        s_idle_since_us = now_us;
        s_drain_at_us = 0;
        portEXIT_CRITICAL(&s_lock);
        notify_state(BT_AUDIO_CONNECTED);
        ESP_LOGI(TAG, "connected to configured speaker");
    } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        audio_stop();
        portENTER_CRITICAL(&s_lock);
        if (s_reconnect_requested) {
            bluetooth_audio_policy_init(&s_policy);
            s_reconnect_requested = false;
        } else {
            bluetooth_audio_policy_on_disconnected(&s_policy, now_us);
        }
        s_media_state = MEDIA_IDLE;
        s_pcm_kind = PCM_NONE;
        s_drain_at_us = 0;
        portEXIT_CRITICAL(&s_lock);
        notify_state(BT_AUDIO_DISABLED);
        wake_worker();
        ESP_LOGW(TAG, "configured speaker disconnected");
    }
}

static void media_acknowledged(const esp_a2d_cb_param_t *param) {
    esp_a2d_media_ctrl_t command = param->media_ctrl_stat.cmd;
    if (command == ESP_A2D_MEDIA_CTRL_START) {
        if (param->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
            media_error("A2DP media start acknowledgement", ESP_FAIL);
            return;
        }
        portENTER_CRITICAL(&s_lock);
        s_media_state = MEDIA_STREAMING;
        portEXIT_CRITICAL(&s_lock);
        set_state(BT_AUDIO_STREAMING);
    } else if (command == ESP_A2D_MEDIA_CTRL_SUSPEND) {
        if (param->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
            media_error("A2DP media suspend acknowledgement", ESP_FAIL);
            return;
        }
        portENTER_CRITICAL(&s_lock);
        s_media_state = MEDIA_IDLE;
        s_pcm_kind = PCM_NONE;
        s_idle_since_us = esp_timer_get_time();
        s_drain_at_us = 0;
        portEXIT_CRITICAL(&s_lock);
        set_state(BT_AUDIO_CONNECTED);
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
                portENTER_CRITICAL(&s_lock);
                s_media_state = MEDIA_STREAMING;
                portEXIT_CRITICAL(&s_lock);
                set_state(BT_AUDIO_STREAMING);
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
    portENTER_CRITICAL(&s_lock);
    should_connect = s_source_ready && s_policy.state != BT_AUDIO_ERROR &&
                     bluetooth_audio_policy_should_connect(&s_policy, now_us);
    portEXIT_CRITICAL(&s_lock);
    if (!should_connect) {
        return;
    }

    notify_state(BT_AUDIO_CONNECTING);
    esp_err_t error = esp_a2d_source_connect((uint8_t *)s_speaker_bda);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "A2DP connection request failed immediately: %s", esp_err_to_name(error));
        portENTER_CRITICAL(&s_lock);
        bluetooth_audio_policy_on_disconnected(&s_policy, now_us);
        portEXIT_CRITICAL(&s_lock);
        notify_state(BT_AUDIO_DISABLED);
    }
}

static void process_drain(int64_t now_us) {
    bool suspend = false;
    portENTER_CRITICAL(&s_lock);
    if (s_drain_at_us != 0 && now_us >= s_drain_at_us && s_media_state == MEDIA_STREAMING) {
        s_drain_at_us = 0;
        s_media_state = MEDIA_SUSPENDING;
        suspend = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!suspend) {
        return;
    }

    esp_err_t error = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
    if (error != ESP_OK) {
        media_error("A2DP media suspend", error);
    }
}

static void process_keepalive(int64_t now_us) {
#if APP_FEATURE_SPEAKER_KEEPALIVE
    bool due;
    portENTER_CRITICAL(&s_lock);
    due = s_policy.state == BT_AUDIO_CONNECTED && s_media_state == MEDIA_IDLE && s_pcm_kind == PCM_NONE &&
          s_idle_since_us != 0 &&
          now_us - s_idle_since_us >= (int64_t)APP_SPEAKER_KEEPALIVE_IDLE_MS * 1000;
    portEXIT_CRITICAL(&s_lock);
    if (!due || !audio_start_silence(APP_SPEAKER_KEEPALIVE_STREAM_MS)) {
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_pcm_kind = PCM_KEEPALIVE;
    portEXIT_CRITICAL(&s_lock);
    if (start_media() != ESP_OK) {
        audio_stop();
    }
#else
    (void)now_us;
#endif
}

static void bluetooth_worker(void *arg) {
    (void)arg;
    for (;;) {
        int64_t now_us = esp_timer_get_time();
        attempt_connection(now_us);
        process_drain(now_us);
        process_keepalive(now_us);
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
    s_idle_since_us = esp_timer_get_time();
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
    pcm_kind_t pcm_kind = s_pcm_kind;
    bool media_available = s_media_state != MEDIA_SUSPENDING;
    if ((state == BT_AUDIO_CONNECTED || state == BT_AUDIO_STREAMING) && media_available) {
        s_drain_at_us = 0;
    }
    portEXIT_CRITICAL(&s_lock);

    if ((state != BT_AUDIO_CONNECTED && state != BT_AUDIO_STREAMING) || !media_available) {
        return BT_AUDIO_PLAY_DISCONNECTED;
    }
    if (pcm_kind == PCM_KEEPALIVE) {
        audio_stop();
        portENTER_CRITICAL(&s_lock);
        s_pcm_kind = PCM_NONE;
        portEXIT_CRITICAL(&s_lock);
    }
    if (!audio_start_impact()) {
        return BT_AUDIO_PLAY_BUSY;
    }

    portENTER_CRITICAL(&s_lock);
    s_pcm_kind = PCM_IMPACT;
    portEXIT_CRITICAL(&s_lock);
    if (start_media() != ESP_OK) {
        audio_stop();
        return BT_AUDIO_PLAY_ERROR;
    }
    return BT_AUDIO_PLAY_STARTED;
}

void bluetooth_audio_request_reconnect(void) {
    bool disconnect;
    portENTER_CRITICAL(&s_lock);
    disconnect = s_policy.state == BT_AUDIO_CONNECTED || s_policy.state == BT_AUDIO_STREAMING;
    s_reconnect_requested = disconnect;
    if (!disconnect) {
        bluetooth_audio_policy_init(&s_policy);
    }
    portEXIT_CRITICAL(&s_lock);

    audio_stop();
    if (disconnect) {
        (void)esp_a2d_source_disconnect((uint8_t *)s_speaker_bda);
    } else {
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
