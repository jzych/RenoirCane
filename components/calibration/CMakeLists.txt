idf_component_register(SRCS "calibration_mode.c" "calibration_protocol.c" "calibration_stream.c" INCLUDE_DIRS "include" REQUIRES accelerometer app_config driver esp_timer freertos status_led log)
