#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize battery voltage monitoring (ADC oneshot mode)
 *        Uses ADC1_CH0, 2.5dB attenuation, 12-bit resolution
 *        External divider: 10k+1k, ratio = 1/11
 */
void battery_init(void);

/**
 * @brief Get current battery voltage in millivolts (actual battery side, before divider)
 */
uint32_t battery_get_voltage_mv(void);

/**
 * @brief Get current battery percentage (0-100)
 */
uint8_t battery_get_percentage(void);

#ifdef __cplusplus
}
#endif
