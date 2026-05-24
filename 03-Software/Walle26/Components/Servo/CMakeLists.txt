idf_component_register(SRCS "servo_app.c" "PCA9685.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "driver" "nvs_flash")
