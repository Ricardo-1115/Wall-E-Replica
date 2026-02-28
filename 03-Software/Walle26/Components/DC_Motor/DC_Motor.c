#include <stdio.h>
#include "DC_Motor.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include <stdlib.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * @brief A helper function to calculate power of a number. This is used to calculate the duty value based on the speed percentage and duty resolution.
 * Note: This function is not optimized for performance and is only intended for small exponent values (
*/
/*
 * 预计算最大占空比，避免每次调用都重新计算。LEDC_DUTY_RES 为位宽（13）。
 */
static const uint32_t MAX_DUTY = ((1u << LEDC_DUTY_RES) - 1);

/*
 * 根据 -100..100 的速度值计算占空比（使用绝对值），方向由 GPIO 控制。
 */
static inline uint32_t calculate_duty(int8_t speed)
{
    return (uint32_t)((abs(speed) * MAX_DUTY) / MOTOR_SPEED_MAX);
}

/*
 * 跟踪当前两个电机的目标速度，供平滑过渡使用。
 * 初始为 0（停止），仅由 DC_Motor_SetSpeed 修改。
 */
int8_t current_speed[2] = {0, 0};

/*
 * @brief Initialize the DC motor by configuring the GPIO pins and setting up the LEDC timer and channel for PWM control.
 * @return esp_err_t indicating success or failure of the initialization.
 * This function configures the GPIO pins for motor control, sets up the LEDC timer with the specified frequency and duty resolution, and configures the LEDC channel to output PWM signals to control the motor speed.
*/
esp_err_t DC_Motor_Init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        /* OR together each pin mask separately to avoid operator precedence bugs */
        .pin_bit_mask = (1ULL << DC_Motor_LEFT_GPIO_1) |
                        (1ULL << DC_Motor_LEFT_GPIO_2) |
                        (1ULL << DC_Motor_RIGHT_GPIO_1) |
                        (1ULL << DC_Motor_RIGHT_GPIO_2),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    /* If fade API is available, install fade function (safe to call multiple times) */
    ledc_fade_func_install(0);

    ledc_channel_config_t ledc_left_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_LEFT_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_left_channel));

    ledc_channel_config_t ledc_right_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL + 1, // Use a different channel for the right motor
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_RIGHT_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_right_channel));

    /*
     * 设置方向引脚的初始电平为安全状态（都拉低），避免上电时 H 桥处于未定义状态。
     */
    gpio_set_level(DC_Motor_LEFT_GPIO_1, 0);
    gpio_set_level(DC_Motor_LEFT_GPIO_2, 0);
    gpio_set_level(DC_Motor_RIGHT_GPIO_1, 0);
    gpio_set_level(DC_Motor_RIGHT_GPIO_2, 0);

    return ESP_OK;
}


/*
 * @brief Stop the specified motor by setting its speed to zero and putting it into standby mode.
 * @param motor_id The ID of the motor to stop (MOTOR_LEFT or MOTOR_RIGHT).
 * @return esp_err_t indicating success or failure of the operation.
*/
esp_err_t DC_Motor_Stop(motor_id_t motor_id)
{
    gpio_set_level(DC_MOTOR_STBY, 0); // 拉低 STBY 引脚进入待机状态
    return DC_Motor_SetSpeedSmooth(motor_id, 0, 300); // 平滑过渡到速度 0，持续 300 ms
}


/*
 * 平滑改变速度（blocking）。
 * - motor_id: 电机编号
 * - target_speed: -100..100
 * - duration_ms: 从当前速度到目标速度的总时长（毫秒）。
 *   若为 0，则直接设置目标速度。
 */
esp_err_t DC_Motor_SetSpeedSmooth(motor_id_t motor_id, int8_t target_speed, uint32_t duration_ms)
{
    /* verify the motor id is valid */
    if (motor_id != MOTOR_LEFT && motor_id != MOTOR_RIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    /* clamp requested speed into allowable range */
    if (target_speed > MOTOR_SPEED_MAX) target_speed = MOTOR_SPEED_MAX;
    if (target_speed < MOTOR_SPEED_MIN) target_speed = MOTOR_SPEED_MIN;

    /* zero duration means no smoothing; just jump to target */
    if (duration_ms == 0) {
        return DC_Motor_SetSpeed(motor_id, target_speed);
    }

    /* compute delta and number of one-unit steps required */
    int8_t start = current_speed[motor_id];
    int delta = (int)target_speed - (int)start;
    int steps = abs(delta);
    if (steps == 0) return ESP_OK; // already at target

    /* delay between each incremental change */
    uint32_t step_delay = duration_ms / (uint32_t)steps;
    int8_t step_dir = (delta > 0) ? 1 : -1; // direction up or down

    for (int i = 1; i <= steps; ++i) {
        int8_t s = start + (int8_t)(i * step_dir); // intermediate speed
        esp_err_t err = DC_Motor_SetSpeed(motor_id, s);
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(step_delay));
    }

    /* final call to ensure exact target (avoids rounding issues) */
    return DC_Motor_SetSpeed(motor_id, target_speed);
}

/*
 * @brief Set the speed of the DC motor by adjusting the PWM duty cycle and direction based on the input speed percentage.
 * @param speed An integer value in the range of -100 to 100, where negative values indicate reverse direction, positive values indicate forward direction, and zero indicates stop.
 * This function calculates the appropriate duty cycle based on the input speed percentage and updates the LEDC channel configuration to control the motor speed. It also sets the GPIO levels to control the direction of the motor.
 * Note: The function clamps the input speed to the range of -100 to 100 to prevent invalid duty cycle values.
 * The duty cycle is calculated as (speed * (2^duty_resolution)) / 100, where duty_resolution is defined by LEDC_DUTY_RES. For example, if the duty resolution is 13 bits, the maximum duty value is 8191 (2^13 - 1), and a speed of 50% would correspond to a duty value of approximately 4096.
*/
esp_err_t DC_Motor_SetSpeed(motor_id_t motor_id, int8_t speed)
{
    /* clamp speed to allowable range; prevents PWM overflow */
    if (speed > MOTOR_SPEED_MAX) {
        speed = MOTOR_SPEED_MAX;
    } else if (speed < MOTOR_SPEED_MIN) {
        speed = MOTOR_SPEED_MIN;
    }

    /* set direction pins depending on sign of speed */
    if (motor_id == MOTOR_LEFT) {
        if (speed > 0) {
            /* forward rotation */
            gpio_set_level(DC_Motor_LEFT_GPIO_1, 1);
            gpio_set_level(DC_Motor_LEFT_GPIO_2, 0);
        } else if (speed < 0) {
            /* reverse rotation */
            gpio_set_level(DC_Motor_LEFT_GPIO_1, 0);
            gpio_set_level(DC_Motor_LEFT_GPIO_2, 1);
        } else {
            /* zero speed; leave both direction pins low for safety */
            gpio_set_level(DC_Motor_LEFT_GPIO_1, 0);
            gpio_set_level(DC_Motor_LEFT_GPIO_2, 0);
        }

        /* compute duty cycle from speed percentage and update channel */
        uint32_t duty = calculate_duty(speed);
        esp_err_t err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
        if (err != ESP_OK) return err;
        err = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        if (err != ESP_OK) return err;

    } else if (motor_id == MOTOR_RIGHT) {
        if (speed > 0) {
            gpio_set_level(DC_Motor_RIGHT_GPIO_1, 1);
            gpio_set_level(DC_Motor_RIGHT_GPIO_2, 0);
        } else if (speed < 0) {
            gpio_set_level(DC_Motor_RIGHT_GPIO_1, 0);
            gpio_set_level(DC_Motor_RIGHT_GPIO_2, 1);
        } else {
            gpio_set_level(DC_Motor_RIGHT_GPIO_1, 0);
            gpio_set_level(DC_Motor_RIGHT_GPIO_2, 0);
        }

        uint32_t duty = calculate_duty(speed);
        esp_err_t err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL + 1, duty);
        if (err != ESP_OK) return err;
        err = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL + 1);
        if (err != ESP_OK) return err;
    } else {
        /* invalid motor id */
        return ESP_ERR_INVALID_ARG;
    }

    /* store the new speed for future smoothing calls */
    if (motor_id == MOTOR_LEFT) {
        current_speed[MOTOR_LEFT] = speed;
    } else {
        current_speed[MOTOR_RIGHT] = speed;
    }

    return ESP_OK;
}




