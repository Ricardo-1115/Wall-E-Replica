idf_component_register(SRCS "wifi.c" "camera_app.c"
                    INCLUDE_DIRS "include"
                    EMBED_FILES "index.html"
                    REQUIRES esp_wifi esp_http_server esp_event nvs_flash json DC_Motor Servo animation_engine energy_manager esp32-camera)