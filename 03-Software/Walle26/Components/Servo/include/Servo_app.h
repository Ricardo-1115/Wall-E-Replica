#pragma once

#include <stdint.h>
#include "Servo.h"
#include <stdbool.h>
#include "command.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C"
{
#endif
    // 专门用于 WebSocket 通信的结构体
    typedef struct {
        float percentages[7];     // 对于 0-6 号关节对应的百分比
        uint32_t duration_ms; // 动作完成的期望时间 (核心：支持手柄的固定值或动画的自定义值)
    } servo_cmd_t;
    extern QueueHandle_t servo_cmd_queue;

    extern JointConfig walle_joint[7];    

	// Function declarations
	// NOTE: servo hardware initialization and app startup are separated.
	// Call servo_hw_init() before calling servo_app_init().
	esp_err_t servo_hw_init(i2c_master_bus_handle_t bus_handle);
	void servo_app_init(void);
    void walle_move_servo(uint8_t channel, float target_angle, uint32_t duration_ms);
    void walle_set_joint_move(uint8_t channel, float percentage, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

