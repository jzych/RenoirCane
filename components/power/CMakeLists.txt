idf_component_register(SRCS "power.c" "power_button.c" INCLUDE_DIRS "include"
    REQUIRES accelerometer app_config bluetooth driver esp_hw_support freertos log status_led)
