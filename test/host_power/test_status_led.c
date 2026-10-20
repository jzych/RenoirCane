#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"

/* Exercise the actual critical sections with contending native threads. */
static pthread_mutex_t led_lock = PTHREAD_MUTEX_INITIALIZER;
#undef portENTER_CRITICAL
#undef portEXIT_CRITICAL
#define portENTER_CRITICAL(lock) ((void)(lock), (void)pthread_mutex_lock(&led_lock))
#define portEXIT_CRITICAL(lock) ((void)(lock), (void)pthread_mutex_unlock(&led_lock))
#include "../../components/status_led/status_led.c"

static void (*worker)(void *);
static bool fail_task;
static jmp_buf tick_end;
static unsigned levels[40], bit_count, frames;
static uint8_t frame[12], rgb[3];

BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack,
                      void *arg, UBaseType_t priority, TaskHandle_t *out) {
    (void)name; (void)stack; (void)arg; (void)priority;
    if (fail_task) return pdFALSE;
    worker = fn; *out = (void *)1; return pdPASS;
}
void vTaskDelay(TickType_t ticks) {
    assert(ticks > 0 && ticks <= 10);
    longjmp(tick_end, 1);
}
static void tick(void) {
    assert(worker);
    if (setjmp(tick_end) == 0) worker(NULL);
}
esp_err_t gpio_config(const gpio_config_t *cfg) {
    assert(cfg->mode == GPIO_MODE_OUTPUT);
    return ESP_OK;
}
esp_err_t rtc_gpio_hold_dis(gpio_num_t pin) { assert(pin == 13); return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
    levels[pin] = level;
    if (pin == 12 && level == 1) {
        assert(levels[13] == 0);
        frame[bit_count / 8] = (uint8_t)((frame[bit_count / 8] << 1) | levels[2]);
        if (++bit_count == 96) {
            assert(memcmp(frame, "\0\0\0\0", 4) == 0 && frame[4] == 0xE2);
            for (unsigned i = 8; i < 12; ++i) assert(frame[i] == 0xFF);
            rgb[0] = frame[7]; rgb[1] = frame[6]; rgb[2] = frame[5];
            const uint8_t valid[][3] = {
                {8,8,8}, {0,0,16}, {0,16,0}, {0,12,12},
                {16,0,0}, {16,8,0}, {0,16,16},
            };
            bool found = false;
            for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
                if (memcmp(rgb, valid[i], 3) == 0) found = true;
            assert(found);
            bit_count = 0; ++frames;
        }
    }
    sched_yield(); /* Make unprotected bit-banging interleave readily. */
    return ESP_OK;
}
static void color(unsigned r, unsigned g, unsigned b) {
    assert(levels[13] == 0 && rgb[0] == r && rgb[1] == g && rgb[2] == b);
}
static void *producer(void *arg) {
    const status_led_state_t *states = arg;
    for (unsigned i = 0; i < 500; ++i) status_led_set(states[i % 2]);
    return NULL;
}
int main(int argc, char **argv) {
    (void)argv;
    fail_task = argc > 1;
    audio_init();
    status_led_init();
    if (fail_task) {
        color(16, 0, 0);
        status_led_on_bluetooth_state(BT_AUDIO_CONNECTED);
        color(16, 0, 0);
        puts("PASS: monitor allocation failure remains visible as red");
        return 0;
    }
    status_led_set(STATUS_LED_BOOT);
    tick(); color(8, 8, 8);
    status_led_on_bluetooth_state(BT_AUDIO_CONNECTING); color(0, 0, 16);
    status_led_on_bluetooth_state(BT_AUDIO_CONNECTED); color(0, 16, 0);
    status_led_on_bluetooth_state(BT_AUDIO_STREAMING);
    tick(); color(0, 16, 0); /* Silence keep-alive is idle. */
    unsigned idle_frames = frames;
    tick(); assert(frames == idle_frames); /* Avoid continuous bit-banging. */
    assert(audio_start_impact());
    tick(); color(0, 12, 12);
    uint8_t pcm[1764]; /* 10 ms, 44.1 kHz stereo signed 16-bit. */
    for (unsigned i = 0; i < 100; ++i) {
        assert(audio_fill_pcm(pcm, sizeof(pcm)) == sizeof(pcm));
        tick();
        if (i < 99) color(0, 12, 12);
    }
    color(0, 16, 0);
    assert(audio_start_impact()); tick(); color(0, 12, 12);
    status_led_on_bluetooth_state(BT_AUDIO_DISABLED);
    color(0, 0, 16); tick(); color(0, 0, 16); /* Active cue cannot mask link loss. */
    audio_stop();
    status_led_on_bluetooth_state(BT_AUDIO_ERROR); tick(); color(16, 0, 0);
    status_led_set((status_led_state_t)-1); color(16, 0, 0);

    unsigned before = frames;
    const status_led_state_t a[] = {STATUS_LED_READY, STATUS_LED_PLAYING};
    const status_led_state_t b[] = {STATUS_LED_BT_CONNECTING, STATUS_LED_ERROR};
    pthread_t threads[2];
    assert(pthread_create(&threads[0], NULL, producer, (void *)a) == 0);
    assert(pthread_create(&threads[1], NULL, producer, (void *)b) == 0);
    for (unsigned i = 0; i < 2; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(frames == before + 1000 && bit_count == 0);
    status_led_set(STATUS_LED_ENTERING_STANDBY); color(16, 8, 0);
    status_led_on_bluetooth_state(BT_AUDIO_CONNECTED);
    tick(); color(16, 8, 0);
    status_led_power_off();
    before = frames;
    assert(audio_start_impact());
    status_led_on_bluetooth_state(BT_AUDIO_STREAMING);
    status_led_on_bluetooth_state(BT_AUDIO_DISABLED);
    tick(); assert(levels[13] == 1 && frames == before);
    puts("PASS: idle/cue/end/disconnect/error, 1000 concurrent frames, standby priority");
    return 0;
}
