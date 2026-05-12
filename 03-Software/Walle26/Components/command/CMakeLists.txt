idf_component_register(SRCS "DFPlayerMini_cmd.c" "DC_Motor_cmd.c" "servo_cmd.c" "nvs_cmd.c" "system.c" "anim_debug.c"
                    INCLUDE_DIRS "include"
                    REQUIRES Servo DC_Motor console nvs_flash DFPlayerMini esp_timer spi_flash animation_engine)
