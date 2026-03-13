idf_component_register(SRCS "DC_Motor.c"  "cmd_motor.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver console spi_flash esp_timer json)
