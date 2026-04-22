idf_component_register(SRCS "wifi.c" "camera_app.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_http_server esp_event nvs_flash json DC_Motor Servo esp32-camera) 