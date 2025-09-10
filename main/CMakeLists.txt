idf_component_register(SRCS "src/esp32_cam.cpp" "src/wifi.cpp" "src/memory.cpp" "src/camera.cpp" "src/ntp_sync.cpp" "src/acces_point.cpp" "src/web_server.cpp" "src/comm_esp_now.cpp"
                    INCLUDE_DIRS "." "include"
                    REQUIRES  "esp_wifi" "esp_netif" "esp_event" "nvs_flash" "esp32-camera" "lwip" "esp_http_server"
                    )
set(EXTRA_COMPONENT_DIRS ${CMAKE_SOURCE_DIR}/components)