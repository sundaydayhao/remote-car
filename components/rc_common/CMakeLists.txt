idf_component_register(SRCS "rc_protocol.c" "rc_network.c" "rc_audio.c" "rc_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_wifi esp_netif nvs_flash lwip esp_driver_i2s esp_driver_i2c esp_codec_dev esp-sr)
