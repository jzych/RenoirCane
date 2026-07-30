#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void audio_init(void);
bool audio_start_impact(void);
uint32_t audio_impact_duration_us(void);
bool audio_start_silence(uint32_t duration_ms);
bool audio_is_active(void);
int32_t audio_fill_pcm(uint8_t *dst, int32_t requested_bytes);
void audio_stop(void);
