idf_component_register(SRCS "main.c" "szpi_board.c" "motor.c" "car_server.c"
    INCLUDE_DIRS "." REQUIRES rc_common esp32-camera esp_http_server esp_driver_gpio)
