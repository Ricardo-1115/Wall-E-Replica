#ifndef DC_MOTOR_H
#define DC_MOTOR_H

#include <stdio.h>
#include <stdlib.h> 
#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"      // 【新增】修复 ESP_LOGI 找不到的问题
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h" 
#include <sys/param.h>
#include "cJSON.h"

// 【修改】正确定义指令结构体类型，供所有包含此头文件的 C 文件使用
typedef struct {
    int8_t left_speed;
    int8_t right_speed;
} motor_cmd_t;

// 【保留】声明全局队列和任务函数
extern QueueHandle_t motor_cmd_queue; 
extern void motor_control_task(void *pvParameters);


#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_LEFT_IO     (5) // Define the output GPIO for the left motor
#define LEDC_OUTPUT_RIGHT_IO    (6) // Define the output GPIO for the right motor 
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz

#define DC_MOTOR_STBY GPIO_NUM_15  // Standby pin for motor driver (LOW = Standby, HIGH = Active)
#define DC_Motor_LEFT_GPIO_1 GPIO_NUM_11
#define DC_Motor_LEFT_GPIO_2 GPIO_NUM_12
#define DC_Motor_RIGHT_GPIO_1 GPIO_NUM_13
#define DC_Motor_RIGHT_GPIO_2 GPIO_NUM_14
#define MOTOR_SPEED_MIN -100
#define MOTOR_SPEED_MAX 100

typedef enum {
    MOTOR_LEFT = 0,
    MOTOR_RIGHT = 1,
}motor_id_t;

typedef enum {
    MOTOR_STATE_IDLE = 0,
    MOTOR_STATE_TO_0,
    MOTOR_STATE_TO_TARGET
} motor_state_t;

typedef struct {
    motor_id_t id;
    ledc_channel_t channel;
    int gpio1;
    int gpio2;
    motor_state_t state;
    int8_t current_speed;
    int8_t target_speed;
    uint32_t stage1_duration_ms;
    uint32_t stage2_duration_ms;
    SemaphoreHandle_t sem;
} motor_control_t;



extern int8_t current_speed[2]; // 用于记录当前速度，供平滑过渡使用

// 初始化后初次设置速度建议使用 DC_Motor_SetSpeedSmooth 来避免突然的速度跳变引起的堵转电流峰值。
esp_err_t DC_Motor_Init(void);

esp_err_t DC_Motor_SetSpeed(motor_id_t motor_id, int8_t speed);
esp_err_t DC_Motor_Stop(uint32_t duration_ms);
esp_err_t DC_Motor_SetSpeedSmooth(motor_id_t motor_id, int8_t target_speed, uint32_t duration_ms);
esp_err_t DC_Motor_SetSpeedSmoothAsync(motor_id_t motor_id, int8_t target_speed, uint32_t duration_ms);


void register_hello(void);
void register_echo(void);
void register_motor_set(void);
void register_motor_stop(void);

#endif // DC_MOTOR_H
