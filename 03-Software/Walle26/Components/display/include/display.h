#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Demo OLED UI (legacy, shows static text)
 */
void example_demo_oled_ui(void);

/**
 * @brief Show battery status on OLED
 * @param percent    Battery percentage 0-100
 * @param voltage_mv Battery voltage in millivolts (before divider, 0 = hide)
 */
void display_battery_show(uint8_t percent, uint32_t voltage_mv);

#ifdef __cplusplus
}
#endif
