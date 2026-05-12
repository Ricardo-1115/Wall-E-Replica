#pragma once

#include <stdio.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"
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

#define SERVO_MIN_TICK  135
#define SERVO_MAX_TICK  519

#ifdef __cplusplus
extern "C"
{
#endif

	// Function declarations
	// 使用外部 I2C 总线初始化（支持一主多从）
	esp_err_t pca9685_init_with_bus(i2c_master_bus_handle_t bus_handle);
	
	void pca9685_set_freq(float freq);
	void pca9685_set_angle(uint8_t Num, float Angle);


#ifdef __cplusplus
}
#endif

