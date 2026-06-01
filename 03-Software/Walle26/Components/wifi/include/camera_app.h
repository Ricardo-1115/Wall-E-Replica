#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the camera (OV2640 on ESP32-S3)
 *
 * Configures GPIO pins, LEDC XCLK, and the esp32-camera driver.
 * Must be called after WiFi init but before stream server start.
 */
void init_camera(void);

#ifdef __cplusplus
}
#endif
