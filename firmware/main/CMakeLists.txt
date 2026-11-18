idf_component_register(SRCS "main.c" "display.c" "app_ui.c" "app_manager.c" "app_runtime.c" "codex_monitor.c" "codex_uart.c" "touch.c" "touch_format.c" "joystick.c" "mouse.c" "touchpad.c" "usb_mouse.c" REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_uart esp_adc esp_lcd esp_hw_support esp_mm usb espressif__tinyusb st7701)
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
