set(SOURCES "andonconsole.c")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES
        tft                # For tft.h, tftspi.h, and tft_controller.h
        tft_controller     # For tft_controller.h
        driver             # For driver/gpio.h
        json              # For cJSON.h
        nvs_flash          # For nvs_flash.h
        esp_wifi
        esp_websocket_client
)
