idf_component_register(SRCS "main.c" "dht22.c"
                    INCLUDE_DIRS "."
                    REQUIRES json lwip esp_wifi nvs_flash esp_netif protocol_examples_common mqtt driver esp_event)