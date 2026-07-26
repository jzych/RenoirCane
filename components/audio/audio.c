#include "audio.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

extern const uint8_t impact_pcm_start[];
extern const uint8_t impact_pcm_end[];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static const uint8_t *s_data;
static size_t s_asset_length;
static size_t s_length;
static size_t s_offset;
static bool s_active;
static bool s_silence;

void audio_init(void) { s_data = impact_pcm_start; s_asset_length = (size_t)(impact_pcm_end - impact_pcm_start); }
bool audio_start_impact(void) { portENTER_CRITICAL(&s_lock); bool ok = !s_active; if (ok) { s_offset = 0; s_length = s_asset_length; s_silence = false; s_active = true; } portEXIT_CRITICAL(&s_lock); return ok; }
bool audio_start_silence(uint32_t duration_ms) { portENTER_CRITICAL(&s_lock); bool ok = !s_active; if (ok) { s_offset = 0; s_length = (size_t)duration_ms * 44100 * 2 * 2 / 1000; s_silence = true; s_active = true; } portEXIT_CRITICAL(&s_lock); return ok; }
bool audio_is_active(void) { portENTER_CRITICAL(&s_lock); bool value = s_active; portEXIT_CRITICAL(&s_lock); return value; }
int32_t audio_fill_pcm(uint8_t *dst, int32_t requested_bytes) { if (requested_bytes <= 0) return 0; portENTER_CRITICAL(&s_lock); size_t remaining = s_active ? s_length - s_offset : 0; size_t count = remaining < (size_t)requested_bytes ? remaining : (size_t)requested_bytes; if (s_silence) memset(dst, 0, count); else if (count) memcpy(dst, s_data + s_offset, count); if (count < (size_t)requested_bytes) memset(dst + count, 0, (size_t)requested_bytes - count); s_offset += count; if (s_offset >= s_length) s_active = false; portEXIT_CRITICAL(&s_lock); return (int32_t)count; }
