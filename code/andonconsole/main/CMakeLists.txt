idf_component_register(
	SRCS "andonconsole.c"
        INCLUDE_DIRS "."
	REQUIRES esp_wifi,ep_wps esp_netif esp_http_client esp_websocket_client nvs_flash esp_event tft tftspi cJSON
)
