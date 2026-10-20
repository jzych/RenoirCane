#define _DEFAULT_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "esp_log.h"
#include "esp_vfs.h"

static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;
#define portENTER_CRITICAL_SAFE(lock) ((void)(lock), (void)pthread_mutex_lock(&ring_lock))
#define portEXIT_CRITICAL_SAFE(lock) ((void)(lock), (void)pthread_mutex_unlock(&ring_lock))
static FILE *fake_freopen(const char *, const char *, FILE *);
static int fake_setvbuf(FILE *, char *, int, size_t);
#define freopen fake_freopen
#define setvbuf fake_setvbuf
#include "../../components/calibration/calibration_log.c"
#undef freopen
#undef setvbuf

static const esp_vfs_t *console_vfs;
static vprintf_like_t log_writer;
static void (*rom_writer)(char);
static bool fail_register, fail_reopen;
static unsigned reopen_count;
esp_err_t esp_vfs_register(const char *path, const esp_vfs_t *vfs, void *ctx) {
    assert(strcmp(path, "/dev/cal_log") == 0 && ctx == NULL);
    if (fail_register) return ESP_FAIL;
    console_vfs = vfs;
    return ESP_OK;
}
static FILE *fake_freopen(const char *path, const char *mode, FILE *stream) {
    assert(strcmp(path, "/dev/cal_log") == 0 && strcmp(mode, "w") == 0);
    ++reopen_count;
    /* ESP-IDF translate_path() supplies '/' for an exact mount-point match. */
    return fail_reopen || console_vfs->open("/", 0, 0) < 0 ? NULL : stream;
}
static int fake_setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    assert((stream == stdout || stream == stderr) && buf == NULL && mode == _IONBF && size == 0);
    return 0;
}
vprintf_like_t esp_log_set_vprintf(vprintf_like_t writer) { log_writer = writer; return NULL; }
void esp_rom_install_channel_putc(int channel, void (*writer)(char)) {
    if (channel == 1) rom_writer = writer;
    else assert(channel == 2 && writer == NULL);
}
static int log_text(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = log_writer(format, args);
    va_end(args);
    return result;
}
static size_t drain(char *text, size_t capacity) {
    size_t length = 0, read;
    while ((read = calibration_log_read((uint8_t *)text + length, capacity - length)) != 0) length += read;
    return length;
}
static void *producer(void *arg) {
    const char *line = arg;
    for (unsigned i = 0; i < 50; ++i) assert(log_text("%s", line) == 4);
    return NULL;
}
int main(void) {
    fail_register = true;
    assert(!calibration_log_init() && !log_writer);
    fail_register = false; fail_reopen = true;
    assert(!calibration_log_init());
    assert(console_vfs->open("/", 0, 0) == 0);
    assert(log_writer); /* Main-task logs must survive a failed freopen. */
    assert(console_vfs->open("/other", 0, 0) == -1);
    fail_reopen = false;
    assert(calibration_log_init() && log_writer && console_vfs && rom_writer);
    unsigned reopens = reopen_count;
    assert(calibration_log_init() && reopen_count == reopens); /* Never clear live logs. */

    char output[8192] = {0};
    assert(log_text("I (%d) BT: %s\n", 123, "connected") == 22);
    assert(console_vfs->write(0, "printf\n", 7) == 7);
    rom_writer('R'); rom_writer('\n');
    size_t length = drain(output, sizeof(output));
    assert(length == 31 && memcmp(output, "I (123) BT: connected\nprintf\nR\n", length) == 0);

    char long_text[601];
    memset(long_text, 'Z', 600); long_text[600] = 0;
    assert(log_text("%s", long_text) == 600);
    length = drain(output, sizeof(output));
    assert(length == 256 && memcmp(output + 245, "[truncated]", 11) == 0);
    assert(calibration_log_take_dropped_bytes() == 355); /* 245 original bytes + 11-byte marker. */

    memset(output, 'X', 4096);
    assert(console_vfs->write(0, output, 4096) == 4096);
    assert(console_vfs->write(0, "overflow", 8) == 8);
    assert(calibration_log_take_dropped_bytes() == 8);
    assert(calibration_log_take_dropped_bytes() == 0);
    assert(drain(output, sizeof(output)) == 4096);
    for (size_t i = 0; i < 4096; ++i) assert(output[i] == 'X');
    assert(log_text("after wrap\n") == 11);
    assert(drain(output, sizeof(output)) == 11 && memcmp(output, "after wrap\n", 11) == 0);

    pthread_t threads[4];
    const char *lines[] = {"AAA\n", "BBB\n", "CCC\n", "DDD\n"};
    for (unsigned i = 0; i < 4; ++i) assert(pthread_create(&threads[i], NULL, producer, (void *)lines[i]) == 0);
    for (unsigned i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    length = drain(output, sizeof(output));
    assert(length == 800 && calibration_log_take_dropped_bytes() == 0);
    unsigned counts[4] = {0};
    for (size_t i = 0; i < length; i += 4) {
        unsigned index = (unsigned)(output[i] - 'A');
        assert(index < 4 && output[i + 1] == output[i] && output[i + 2] == output[i] && output[i + 3] == '\n');
        ++counts[index];
    }
    for (unsigned i = 0; i < 4; ++i) assert(counts[i] == 50);
    /* Transport losses must add to concurrent capture loss, then saturate. */
    calibration_log_record_dropped_bytes(8);
    count_dropped(5);
    calibration_log_record_dropped_bytes(23);
    assert(calibration_log_take_dropped_bytes() == 36);
    calibration_log_record_dropped_bytes(UINT32_MAX - 10);
    calibration_log_record_dropped_bytes(23);
    assert(calibration_log_take_dropped_bytes() == UINT32_MAX);
    assert(calibration_log_take_dropped_bytes() == 0);
    puts("PASS: log/stdio/ROM capture, truncation, overflow, wrap, concurrent producers, init failures");
    return 0;
}
