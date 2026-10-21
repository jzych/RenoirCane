#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define IRAM_ATTR
#define ESP_INTR_FLAG_IRAM 1
static inline void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(tag, ...) test_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_log(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) test_log(tag, __VA_ARGS__)
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "test error"; }

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void *TaskHandle_t;
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define portENTER_CRITICAL_ISR(lock) ((void)(lock))
#define portEXIT_CRITICAL_ISR(lock) ((void)(lock))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portYIELD_FROM_ISR() ((void)0)
BaseType_t xTaskCreate(void (*fn)(void *), const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *, BaseType_t);
BaseType_t xPortGetCoreID(void);
void xTaskNotifyGive(TaskHandle_t);
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
TickType_t xTaskGetTickCount(void);
int64_t esp_timer_get_time(void);

typedef uint32_t EventBits_t;
typedef struct { EventBits_t bits; } *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t, BaseType_t, BaseType_t, TickType_t);

#define ESP_BD_ADDR_LEN 6
typedef uint8_t esp_bd_addr_t[ESP_BD_ADDR_LEN];
typedef enum { ESP_A2D_CONNECTION_STATE_EVT, ESP_A2D_MEDIA_CTRL_ACK_EVT,
               ESP_A2D_AUDIO_STATE_EVT, ESP_A2D_PROF_STATE_EVT } esp_a2d_cb_event_t;
typedef enum { ESP_A2D_CONNECTION_STATE_DISCONNECTED, ESP_A2D_CONNECTION_STATE_CONNECTING,
               ESP_A2D_CONNECTION_STATE_CONNECTED } esp_a2d_connection_state_t;
typedef enum { ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY, ESP_A2D_MEDIA_CTRL_START,
               ESP_A2D_MEDIA_CTRL_SUSPEND } esp_a2d_media_ctrl_t;
#define ESP_A2D_MEDIA_CTRL_ACK_SUCCESS 0
#define ESP_A2D_AUDIO_STATE_STARTED 1
#define ESP_A2D_AUDIO_STATE_SUSPEND 2
#define ESP_A2D_DEINIT_SUCCESS 0
#define ESP_A2D_INIT_SUCCESS 1
typedef union {
    struct { esp_a2d_connection_state_t state; esp_bd_addr_t remote_bda; } conn_stat;
    struct { esp_a2d_media_ctrl_t cmd; int status; } media_ctrl_stat;
    struct { int state; } audio_stat;
    struct { int init_state; } a2d_prof_stat;
} esp_a2d_cb_param_t;
esp_err_t esp_a2d_source_connect(uint8_t *);
esp_err_t esp_a2d_source_disconnect(uint8_t *);
esp_err_t esp_a2d_media_ctrl(esp_a2d_media_ctrl_t);
static inline esp_err_t esp_a2d_register_callback(void (*cb)(esp_a2d_cb_event_t, esp_a2d_cb_param_t *)) { (void)cb; return ESP_OK; }
static inline esp_err_t esp_a2d_source_register_data_callback(int32_t (*cb)(uint8_t *, int32_t)) { (void)cb; return ESP_OK; }
static inline esp_err_t esp_a2d_source_init(void) { return ESP_OK; }
static inline esp_err_t esp_a2d_source_deinit(void) { return ESP_OK; }

typedef int esp_bt_gap_cb_event_t;
typedef struct { struct { esp_bd_addr_t bda; } cfm_req; } esp_bt_gap_cb_param_t;
#define ESP_BT_GAP_CFM_REQ_EVT 1
#define ESP_BT_IO_CAP_NONE 0
#define ESP_BT_SP_IOCAP_MODE 0
#define ESP_BT_NON_CONNECTABLE 0
#define ESP_BT_NON_DISCOVERABLE 0
static inline esp_err_t esp_bt_gap_ssp_confirm_reply(uint8_t *addr, bool accept) { (void)addr; (void)accept; return ESP_OK; }
static inline esp_err_t esp_bt_gap_set_security_param(int a, void *b, size_t c) { (void)a; (void)b; (void)c; return ESP_OK; }
static inline esp_err_t esp_bt_gap_register_callback(void (*cb)(esp_bt_gap_cb_event_t, esp_bt_gap_cb_param_t *)) { (void)cb; return ESP_OK; }
static inline esp_err_t esp_bt_gap_set_scan_mode(int a, int b) { (void)a; (void)b; return ESP_OK; }
static inline esp_err_t esp_bt_dev_set_device_name(const char *s) { (void)s; return ESP_OK; }

#define ESP_BT_MODE_BLE 1
#define ESP_BT_MODE_CLASSIC_BT 2
typedef struct { int unused; } esp_bt_controller_config_t;
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() {0}
typedef struct { bool ssp_en; } esp_bluedroid_config_t;
#define BT_BLUEDROID_INIT_CONFIG_DEFAULT() {false}
static inline esp_err_t esp_bt_controller_mem_release(int mode) { (void)mode; return ESP_OK; }
static inline esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *c) { (void)c; return ESP_OK; }
static inline esp_err_t esp_bt_controller_enable(int mode) { (void)mode; return ESP_OK; }
static inline esp_err_t esp_bluedroid_init_with_cfg(esp_bluedroid_config_t *c) { (void)c; return ESP_OK; }
static inline esp_err_t esp_bluedroid_enable(void) { return ESP_OK; }
static inline esp_err_t nvs_flash_init(void) { return ESP_OK; }
