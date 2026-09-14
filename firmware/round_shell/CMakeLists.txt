idf_component_register(SRCS "round_shell.cpp" "round_font_18.c" "round_font_24.c"
    INCLUDE_DIRS "." REQUIRES brookesia_core XiaozhiApp MusicPlayer bsp_extra system_status
    esp_driver_gpio esp_driver_usb_serial_jtag esp_timer esp_wifi esp-audio-player)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
