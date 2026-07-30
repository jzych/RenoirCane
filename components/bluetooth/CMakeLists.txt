idf_component_register(
    SRCS "bluetooth_audio.c" "bluetooth_audio_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config audio bt esp_timer freertos nvs_flash
)
