idf_component_register(SRCS "main.c" "display.c" "touch.c" "touch_format.c" REQUIRES esp_driver_gpio esp_driver_i2c esp_adc esp_lcd esp_hw_support esp_mm st7701)
