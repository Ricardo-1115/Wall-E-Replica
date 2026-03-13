#ifndef PCA9685_H
#define PCA9685_H

#include <stdio.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include "sdkconfig.h"
#include <driver/i2c_master.h>

#define PCA_Addr        0x40        // 从机地址
#define PCA_Model       0x00
#define LED0_ON_L       0x06
#define LED0_ON_H       0x07
#define LED0_OFF_L      0x08
#define LED0_OFF_H      0x09
#define PCA_Pre         0xFE        // 配置频率地址

#define SERVO_MIN_TICK  126
#define SERVO_MAX_TICK  521

#ifdef __cplusplus
extern "C"
{
#endif

	// Function declarations
	void pca9685_init(void);
	void pca9685_set_freq(float freq);
	void pca9685_set_angle(uint8_t Num, float Angle);
	void walle_move_servo(uint8_t channel, float target_angle, uint32_t duration_ms);

	void register_servo(void);

#ifdef __cplusplus
}
#endif

#endif /* PCA9685_H */