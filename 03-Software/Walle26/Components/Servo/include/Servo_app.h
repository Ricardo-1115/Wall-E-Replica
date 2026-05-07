#pragma once

#include <stdint.h>
#include "PCA9685.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define JOINT_COUNT 7

typedef struct {
    float max_angle;          // 组装后的最大角度
    float min_angle;          // 组装后的最小角度
    bool reverse;             // 是否需要反向
} JointConfig;

#ifdef __cplusplus
extern "C"
{
#endif
    // 专门用于 WebSocket 通信的结构体
    typedef struct {
        float percentages[JOINT_COUNT];     // 7 个关节对应的百分比 (0.0 ~ 100.0)
        uint32_t duration_ms; // 动作完成的期望时间
    } servo_cmd_t;
    extern QueueHandle_t servo_cmd_queue;

    extern JointConfig walle_joint[JOINT_COUNT];

	// ======== Function declarations ========
	// NOTE: servo hardware initialization and app startup are separated.
	// Call Servo_hw_Init() before calling Servo_app_Init().

	esp_err_t Servo_hw_Init(i2c_master_bus_handle_t bus_handle);
	void Servo_app_Init(void);

    // 底层：直接设置 PCA9685 通道的绝对角度 (channel 0-15, angle 0-180°)
    void walle_servo_set_angle(uint8_t channel, float angle, uint32_t duration_ms);
    // 上层：关节空间映射 (joint_id 0-6, percentage 0-100, 经 JointConfig 换算后调用 walle_servo_set_angle)
    void walle_joint_move(uint8_t joint_id, float percentage, uint32_t duration_ms);

    float get_servo_angle(uint8_t channel);
    void save_joint_config_to_nvs(void);

#ifdef __cplusplus
}
#endif

