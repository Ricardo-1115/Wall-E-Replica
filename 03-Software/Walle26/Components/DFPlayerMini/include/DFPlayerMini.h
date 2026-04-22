#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C"
{
#endif

	// Function declarations
    esp_err_t DFPlayerMini_Init(void);
    void DFPlayerMini_play_folder(uint8_t folder, uint8_t file);
    void DFPlayerMini_set_volume(uint8_t volume);

#ifdef __cplusplus
}
#endif

