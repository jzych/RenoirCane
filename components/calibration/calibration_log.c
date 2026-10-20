#include "calibration_log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "calibration_protocol.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"

#define LOG_RING_CAPACITY 4096U
#define LOG_VFS_PATH "/dev/cal_log"

/* Internal DRAM plus SAFE critical sections also serve ROM printing from an
 * ISR or with flash cache disabled. No libc/RTOS calls on that producer path. */
static DRAM_ATTR uint8_t s_ring[LOG_RING_CAPACITY];
static DRAM_ATTR size_t s_head, s_tail, s_used;
static DRAM_ATTR uint32_t s_dropped;
static DRAM_ATTR portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_vfs_registered, s_initialized;

static void IRAM_ATTR count_dropped(size_t count) {
    portENTER_CRITICAL_SAFE(&s_lock);
    if (count > UINT32_MAX - s_dropped) s_dropped = UINT32_MAX;
    else s_dropped += (uint32_t)count;
    portEXIT_CRITICAL_SAFE(&s_lock);
}

static void IRAM_ATTR capture_bytes(const uint8_t *data, size_t length) {
    /* Bound each time spent with interrupts disabled, including large writes. */
    while (length != 0U) {
        size_t chunk = length > CALIBRATION_MAX_PAYLOAD ? CALIBRATION_MAX_PAYLOAD : length;
        portENTER_CRITICAL_SAFE(&s_lock);
        size_t available = LOG_RING_CAPACITY - s_used;
        size_t copied = chunk < available ? chunk : available;
        for (size_t i = 0; i < copied; ++i) {
            s_ring[s_head] = data[i];
            s_head = (s_head + 1U) % LOG_RING_CAPACITY;
        }
        s_used += copied;
        size_t dropped = chunk - copied;
        if (dropped > UINT32_MAX - s_dropped) s_dropped = UINT32_MAX;
        else s_dropped += (uint32_t)dropped;
        portEXIT_CRITICAL_SAFE(&s_lock);
        data += chunk;
        length -= chunk;
    }
}

static void IRAM_ATTR capture_rom_char(char c) {
    uint8_t byte = (uint8_t)c;
    capture_bytes(&byte, 1U);
}

static int capture_vprintf(const char *format, va_list args) {
    char text[CALIBRATION_MAX_PAYLOAD + 1U];
    int result = vsnprintf(text, sizeof(text), format, args);
    if (result <= 0) return result;
    size_t length = (size_t)result;
    if (length > CALIBRATION_MAX_PAYLOAD) {
        static const char suffix[] = "[truncated]";
        memcpy(text + CALIBRATION_MAX_PAYLOAD - (sizeof(suffix) - 1U), suffix, sizeof(suffix) - 1U);
        count_dropped(length - (CALIBRATION_MAX_PAYLOAD - (sizeof(suffix) - 1U)));
        length = CALIBRATION_MAX_PAYLOAD;
    }
    capture_bytes((const uint8_t *)text, length);
    return result;
}

static ssize_t console_write(int fd, const void *data, size_t length) {
    (void)fd;
    capture_bytes(data, length);
    return (ssize_t)length;
}

static int console_open(const char *path, int flags, int mode) {
    (void)flags;
    (void)mode;
    /* VFS translates an exact mount-point match to '/', never an empty path. */
    if (strcmp(path, "/") != 0) { errno = ENOENT; return -1; }
    return 0;
}

static int console_close(int fd) { (void)fd; return 0; }

static int console_fstat(int fd, struct stat *st) {
    (void)fd;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR;
    return 0;
}

bool calibration_log_init(void) {
    if (s_initialized) return true;
    static const esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .write = console_write,
        .open = console_open,
        .close = console_close,
        .fstat = console_fstat,
    };
    if (!s_vfs_registered) {
        if (esp_vfs_register(LOG_VFS_PATH, &vfs, NULL) != ESP_OK) return false;
        s_vfs_registered = true;
    }
    /* Flush old bytes at the old baud. freopen changes the shared FILE objects,
     * reaching existing tasks as well as tasks created after this function. */
    if (fflush(stdout) != 0 || fflush(stderr) != 0) return false;
    /* freopen may close a shared FILE even on failure. Capture ESP-IDF/ROM
     * logs first so returning from app_main cannot log through a closed FILE. */
    esp_log_set_vprintf(capture_vprintf);
    esp_rom_install_channel_putc(1, capture_rom_char);
    esp_rom_install_channel_putc(2, NULL);
    if (freopen(LOG_VFS_PATH, "w", stdout) == NULL ||
        freopen(LOG_VFS_PATH, "w", stderr) == NULL) return false;
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0 ||
        setvbuf(stderr, NULL, _IONBF, 0) != 0) return false;
    s_initialized = true;
    return true;
}

size_t calibration_log_read(uint8_t *output, size_t capacity) {
    if (capacity > CALIBRATION_MAX_PAYLOAD) capacity = CALIBRATION_MAX_PAYLOAD;
    portENTER_CRITICAL_SAFE(&s_lock);
    size_t length = capacity < s_used ? capacity : s_used;
    for (size_t i = 0; i < length; ++i) {
        output[i] = s_ring[s_tail];
        s_tail = (s_tail + 1U) % LOG_RING_CAPACITY;
    }
    s_used -= length;
    portEXIT_CRITICAL_SAFE(&s_lock);
    return length;
}

uint32_t calibration_log_take_dropped_bytes(void) {
    portENTER_CRITICAL_SAFE(&s_lock);
    uint32_t dropped = s_dropped;
    s_dropped = 0;
    portEXIT_CRITICAL_SAFE(&s_lock);
    return dropped;
}
