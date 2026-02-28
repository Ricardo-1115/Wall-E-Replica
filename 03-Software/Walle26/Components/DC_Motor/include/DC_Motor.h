#ifndef DC_MOTOR_H
#define DC_MOTOR_H

#include "esp_err.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_LEFT_IO     (5) // Define the output GPIO for the left motor
#define LEDC_OUTPUT_RIGHT_IO    (6) // Define the output GPIO for the right motor 
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz

typedef enum {
    MOTOR_LEFT = 0,
    MOTOR_RIGHT = 1,
}motor_id_t;

#define DC_MOTOR_STBY GPIO_NUM_15  // Standby pin for motor driver (LOW = Standby, HIGH = Active)
#define DC_Motor_LEFT_GPIO_1 GPIO_NUM_11
#define DC_Motor_LEFT_GPIO_2 GPIO_NUM_12
#define DC_Motor_RIGHT_GPIO_1 GPIO_NUM_13
#define DC_Motor_RIGHT_GPIO_2 GPIO_NUM_14
#define MOTOR_SPEED_MIN -100
#define MOTOR_SPEED_MAX 100

extern int8_t current_speed[2]; // 用于记录当前速度，供平滑过渡使用

esp_err_t DC_Motor_Init(void);
// 设置电机速度，返回 esp_err_t 以便上层处理错误。
// speed: -100 .. 100，负值表示反转，正值表示正转，0 表示停止（具体为空转或制动取决于驱动电路）。
esp_err_t DC_Motor_SetSpeed(motor_id_t motor_id, int8_t speed);

// 立即停止电机（封装）— 根据驱动器设定为空转或制动
esp_err_t DC_Motor_Stop(motor_id_t motor_id);

// 平滑改变速度到目标值，持续时间以毫秒为单位（blocking）。
// duration_ms 为 0 时等同于直接调用 DC_Motor_SetSpeed。
esp_err_t DC_Motor_SetSpeedSmooth(motor_id_t motor_id, int8_t target_speed, uint32_t duration_ms);


void register_hello(void);
void register_echo(void);
void register_motor_set(void);
void register_motor_stop(void);

#endif // DC_MOTOR_H
