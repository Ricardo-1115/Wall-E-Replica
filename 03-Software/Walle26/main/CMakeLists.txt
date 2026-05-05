idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES "console" "command" "DC_Motor" "Servo" "nvs_flash" "wifi" "display" "fatfs")
