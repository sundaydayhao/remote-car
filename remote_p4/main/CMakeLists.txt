idf_component_register(SRCS "main.c" "remote_link.c" "remote_ui.c" "video.c"
    INCLUDE_DIRS "." REQUIRES rc_common esp32_p4_function_ev_board esp_http_client esp_jpeg)
