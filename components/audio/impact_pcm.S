.section .rodata.embedded, "a", @progbits
.global impact_pcm_start
.global impact_pcm_end

impact_pcm_start:
.incbin PCM_ASSET_PATH
impact_pcm_end:
