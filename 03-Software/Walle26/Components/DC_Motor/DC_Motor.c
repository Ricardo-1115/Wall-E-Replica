#include "DC_Motor.h"

static motor_control_t motors[2] = {
    {.id = MOTOR_LEFT,
     .channel = LEDC_CHANNEL,
     .gpio1 = DC_Motor_LEFT_GPIO_1,
     .gpio2 = DC_Motor_LEFT_GPIO_2,
     .state = MOTOR_STATE_IDLE,
     .current_speed = 0,
     .target_speed = 0,
     .stage1_duration_ms = 0,
     .stage2_duration_ms = 0,
     .sem = NULL},

    {.id = MOTOR_RIGHT,
     .channel = LEDC_CHANNEL + 1,
     .gpio1 = DC_Motor_RIGHT_GPIO_1,
     .gpio2 = DC_Motor_RIGHT_GPIO_2,
     .state = MOTOR_STATE_IDLE,
     .current_speed = 0,
     .target_speed = 0,
     .stage1_duration_ms = 0,
     .stage2_duration_ms = 0,
     .sem = NULL}};

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

static int8_t duty_to_speed(uint32_t duty, int dir_pin1, int dir_pin2)
{
    int8_t speed = (int8_t)((duty * MOTOR_SPEED_MAX + (MAX_DUTY / 2)) / MAX_DUTY); // 四舍五入
    if (dir_pin1 == 1 && dir_pin2 == 0)
        return speed; // forward
    else if (dir_pin1 == 0 && dir_pin2 == 1)
        return -speed; // reverse
    return 0;
}

static int8_t motor_get_current_speed(motor_control_t *motor)
{
    int dir1 = gpio_get_level(motor->gpio1);
    int dir2 = gpio_get_level(motor->gpio2);
    uint32_t duty = ledc_get_duty(LEDC_MODE, motor->channel);
    return duty_to_speed(duty, dir1, dir2);
}

static void motor_set_direction(motor_control_t *motor, int8_t speed)
{
    if (speed > 0)
    {
        gpio_set_level(motor->gpio1, 1);
        gpio_set_level(motor->gpio2, 0);
    }
    else if (speed < 0)
    {
        gpio_set_level(motor->gpio1, 0);
        gpio_set_level(motor->gpio2, 1);
    }
    else
    {
        gpio_set_level(motor->gpio1, 0);
        gpio_set_level(motor->gpio2, 0);
    }
}

static IRAM_ATTR bool motor_fade_cb(const ledc_cb_param_t *param, void *user_arg)
{
    motor_control_t *motor = (motor_control_t *)user_arg;

    if (param->event != LEDC_FADE_END_EVT)
        return false; // 只处理淡入淡出结束事件

    BaseType_t need_yield = pdFALSE;

    if (motor->state == MOTOR_STATE_TO_0)
    {
        // 第一阶段完成，当前占空比为0，准备反向淡入到目标速度
        motor_set_direction(motor, motor->target_speed); // 先切换方向
        // 启动第二阶段渐变
        uint32_t target_duty = calculate_duty(motor->target_speed);
        ledc_set_fade_with_time(LEDC_MODE, motor->channel, target_duty, motor->stage2_duration_ms);
        ledc_fade_start(LEDC_MODE, motor->channel, LEDC_FADE_NO_WAIT);
        motor->state = MOTOR_STATE_TO_TARGET;
    }
    else if (motor->state == MOTOR_STATE_TO_TARGET)
    {
        // 第二阶段完成，已经达到目标速度
        motor->state = MOTOR_STATE_IDLE;
        motor->current_speed = motor->target_speed;
        if (motor->sem)
        {
            xSemaphoreGiveFromISR(motor->sem, &need_yield); 
        }
    }

    return (need_yield == pdTRUE);
}

esp_err_t DC_Motor_SetSpeedSmoothAsync(motor_id_t motor_id, int8_t target_speed, uint32_t duration_ms)
{
    // 参数验证
    if (motor_id != MOTOR_LEFT && motor_id != MOTOR_RIGHT)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (target_speed > MOTOR_SPEED_MAX || target_speed < MOTOR_SPEED_MIN)
    {
        return ESP_ERR_INVALID_ARG;
    }

    motor_control_t *motor = &motors[motor_id]; // 为什么不直接用motors[motor_id]直接访问？因为我们需要一个指针来传递给回调函数，而 motors[motor_id] 是一个结构体实例，不能直接作为指针使用。通过定义 motor_control_t *motor，我们创建了一个指向该结构体实例的指针，这样我们就可以在回调函数中访问和修改该实例的成员变量。(thanks to chatGPT for the explanation)

    // if motor is currently fading, stop current fade.
    if (motor->state != MOTOR_STATE_IDLE)
    {
        ledc_fade_stop(LEDC_MODE, motor->channel);
        motor->state = MOTOR_STATE_IDLE;
    }

    // get current speed as starting point for fade
    int8_t current_speed = motor_get_current_speed(motor);
    if (current_speed == target_speed)
        return ESP_OK; // already at target

    // if duration is zero, just set speed immediately
    if (duration_ms == 0)
    {
        motor_set_direction(motor, target_speed);
        uint32_t duty = calculate_duty(target_speed);
        ledc_set_duty(LEDC_MODE, motor->channel, duty);
        ledc_update_duty(LEDC_MODE, motor->channel);
        motor->current_speed = target_speed;
        return ESP_OK;
    }

    // 判断方向是否改变，如果改变了需要先淡出到0再淡入到目标速度
    bool direction_change = (current_speed > 0 && target_speed < 0) || (current_speed < 0 && target_speed > 0);

    if (!direction_change)
    {
        // same direction, just fade to target
        motor_set_direction(motor, target_speed);
        motor->target_speed = target_speed;  
        motor->state = MOTOR_STATE_TO_TARGET;
        motor->stage2_duration_ms = duration_ms; // 为什么要这样可是回调函数中没有用到我是说在不用反向的情况下就用不到motor->stage2_duration_ms吧？是的，在不需要反向的情况下，motor->stage2_duration_ms 这个字段确实没有被使用到。这个字段主要是为了在需要先淡出到0再淡入到目标速度的情况下，存储第二阶段的持续时间，以便在回调函数中使用。对于同方向的情况，我们直接使用传入的 duration_ms 来设置淡入时间，而不需要区分阶段，因此 motor->stage2_duration_ms 在这种情况下没有实际作用。不过，为了保持代码的一致性和简洁性，我们仍然将 duration_ms 赋值给 motor->stage2_duration_ms，这样在回调函数中就可以统一使用 motor->stage2_duration_ms 来获取淡入时间，无论是否需要反向。thanks to chatGPT for the explanation
        uint32_t target_duty = calculate_duty(target_speed);
        ledc_set_fade_with_time(LEDC_MODE, motor->channel, target_duty, duration_ms);
        ledc_fade_start(LEDC_MODE, motor->channel, LEDC_FADE_NO_WAIT);
    }
    else
    {
        // direction change, fade to 0 first then fade to target,calculate stage durations based on abs speed difference
        uint32_t cur_abs = abs(current_speed);
        uint32_t tar_abs = abs(target_speed);
        uint32_t total_diff = cur_abs + tar_abs;
        if (total_diff == 0)
            return ESP_OK; // 不应该发生

        motor->stage1_duration_ms = (duration_ms * cur_abs) / total_diff;
        motor->stage2_duration_ms = duration_ms - motor->stage1_duration_ms;

        // first fade to 0 and keep direction unchanged
        motor->target_speed = target_speed;
        motor->state = MOTOR_STATE_TO_0;
        ledc_set_fade_with_time(LEDC_MODE, motor->channel, 0, motor->stage1_duration_ms);
        ledc_fade_start(LEDC_MODE, motor->channel, LEDC_FADE_NO_WAIT);
    }

    return ESP_OK;
}

/*
 * 跟踪当前两个电机的目标速度，供平滑过渡使用。
 * 初始为 0（停止），仅由 DC_Motor_SetSpeed 修改。
 */
int8_t current_speed[2] = {0, 0};

esp_err_t DC_Motor_Init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        /* OR together each pin mask separately to avoid operator precedence bugs */
        .pin_bit_mask = (1ULL << DC_Motor_LEFT_GPIO_1) |
                        (1ULL << DC_Motor_LEFT_GPIO_2) |
                        (1ULL << DC_Motor_RIGHT_GPIO_1) |
                        (1ULL << DC_Motor_RIGHT_GPIO_2),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY, // Set output frequency at 4 kHz
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    /* If fade API is available, install fade function (safe to call multiple times) */
    ledc_fade_func_install(0);

    ledc_channel_config_t ledc_left_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_FADE_END,
        .gpio_num = LEDC_OUTPUT_LEFT_IO,
        .duty = 0, // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_left_channel));

    ledc_channel_config_t ledc_right_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL + 1, // Use a different channel for the right motor
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_FADE_END,
        .gpio_num = LEDC_OUTPUT_RIGHT_IO,
        .duty = 0, // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_right_channel));

    /*
     * 设置方向引脚的初始电平为安全状态（都拉低），避免上电时 H 桥处于未定义状态。
     */
    gpio_set_level(DC_Motor_LEFT_GPIO_1, 0);
    gpio_set_level(DC_Motor_LEFT_GPIO_2, 0);
    gpio_set_level(DC_Motor_RIGHT_GPIO_1, 0);
    gpio_set_level(DC_Motor_RIGHT_GPIO_2, 0);

    ledc_cbs_t callbacks = {
        .fade_cb = motor_fade_cb};
    ledc_cb_register(LEDC_MODE, LEDC_CHANNEL, &callbacks, (void *)&motors[0]);
    ledc_cb_register(LEDC_MODE, LEDC_CHANNEL + 1, &callbacks, (void *)&motors[1]);

    return ESP_OK;
}

/*
 * @brief Stop the DC motor by setting the PWM duty cycle to zero and putting the motor driver into standby mode.
 * @param duration_ms The duration in milliseconds over which to fade the motor speed down to zero. If set to 0, the motor will stop immediately without fading.
 * This function uses the LEDC fade API to smoothly reduce the motor speed to zero over the specified duration. After fading, it sets the standby pin low to put the motor driver into standby mode, which can help reduce power consumption and prevent unintended movement.
 */
esp_err_t DC_Motor_Stop(uint32_t duration_ms)
{
    DC_Motor_SetSpeedSmoothAsync(MOTOR_LEFT, 0, duration_ms);
    DC_Motor_SetSpeedSmoothAsync(MOTOR_RIGHT, 0, duration_ms);

    /* If fade API is available and duration is specified, use it to smoothly stop the motors */

    return ESP_OK;
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
    if (motor_id != MOTOR_LEFT && motor_id != MOTOR_RIGHT)
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* clamp requested speed into allowable range */
    if (target_speed > MOTOR_SPEED_MAX)
        target_speed = MOTOR_SPEED_MAX;
    if (target_speed < MOTOR_SPEED_MIN)
        target_speed = MOTOR_SPEED_MIN;

    /* zero duration means no smoothing; just jump to target */
    if (duration_ms == 0)
    {
        return DC_Motor_SetSpeed(motor_id, target_speed);
    }

    /* compute delta and number of one-unit steps required */
    int8_t start = current_speed[motor_id];
    int delta = (int)target_speed - (int)start;
    int steps = abs(delta);
    if (steps == 0)
        return ESP_OK; // already at target

    /* delay between each incremental change */
    uint32_t step_delay = duration_ms / (uint32_t)steps;
    int8_t step_dir = (delta > 0) ? 1 : -1; // direction up or down

    for (int i = 1; i <= steps; ++i)
    {
        int8_t s = start + (int8_t)(i * step_dir); // intermediate speed
        esp_err_t err = DC_Motor_SetSpeed(motor_id, s);
        if (err != ESP_OK)
            return err;
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
    if (speed > MOTOR_SPEED_MAX)
    {
        speed = MOTOR_SPEED_MAX;
    }
    else if (speed < MOTOR_SPEED_MIN)
    {
        speed = MOTOR_SPEED_MIN;
    }

    /* set direction pins depending on sign of speed */
    if (motor_id == MOTOR_LEFT)
    {
        if (speed > 0)
        {
            /* forward rotation */
            gpio_set_level(DC_Motor_LEFT_GPIO_1, 1);
            gpio_set_level(DC_Motor_LEFT_GPIO_2, 0);
        }
        else if (speed < 0)
        {
            /* reverse rotation */
            gpio_set_level(DC_Motor_LEFT_GPIO_1, 0);
            gpio_set_level(DC_Motor_LEFT_GPIO_2, 1);
        }
        else
        {
            /* zero speed; leave both direction pins low for safety */
            gpio_set_level(DC_Motor_LEFT_GPIO_1, 0);
            gpio_set_level(DC_Motor_LEFT_GPIO_2, 0);
        }

        /* compute duty cycle from speed percentage and update channel */
        uint32_t duty = calculate_duty(speed);
        esp_err_t err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
        if (err != ESP_OK)
            return err;
        err = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        if (err != ESP_OK)
            return err;
    }
    else if (motor_id == MOTOR_RIGHT)
    {
        if (speed > 0)
        {
            gpio_set_level(DC_Motor_RIGHT_GPIO_1, 1);
            gpio_set_level(DC_Motor_RIGHT_GPIO_2, 0);
        }
        else if (speed < 0)
        {
            gpio_set_level(DC_Motor_RIGHT_GPIO_1, 0);
            gpio_set_level(DC_Motor_RIGHT_GPIO_2, 1);
        }
        else
        {
            gpio_set_level(DC_Motor_RIGHT_GPIO_1, 0);
            gpio_set_level(DC_Motor_RIGHT_GPIO_2, 0);
        }

        uint32_t duty = calculate_duty(speed);
        esp_err_t err = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL + 1, duty);
        if (err != ESP_OK)
            return err;
        err = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL + 1);
        if (err != ESP_OK)
            return err;
    }
    else
    {
        /* invalid motor id */
        return ESP_ERR_INVALID_ARG;
    }

    /* store the new speed for future smoothing calls */
    if (motor_id == MOTOR_LEFT)
    {
        current_speed[MOTOR_LEFT] = speed;
    }
    else
    {
        current_speed[MOTOR_RIGHT] = speed;
    }

    return ESP_OK;
}

// 2. 声明全局队列句柄
QueueHandle_t motor_cmd_queue = NULL;

// 3. 定义心跳超时时间 (比如 500 毫秒没收到数据就刹车)
#define HEARTBEAT_TIMEOUT_MS 500

void motor_control_task(void *pvParameters) {
    motor_cmd_t cmd;
    bool is_disconnected = false; // 记录是否处于失控状态

    ESP_LOGI("MOTOR_TASK", "电机控制任务已启动，等待链路指令...");

    while (1) {
        // 核心亮点：xQueueReceive 自带看门狗功能！
        // 如果在 HEARTBEAT_TIMEOUT_MS 内收到了前端的数据，返回 pdPASS
        // 如果超时没收到，返回 errQUEUE_EMPTY
        if (xQueueReceive(motor_cmd_queue, &cmd, pdMS_TO_TICKS(HEARTBEAT_TIMEOUT_MS)) == pdPASS) {
            
            if (is_disconnected) {
                ESP_LOGI("MOTOR_TASK", "链路恢复，解除锁定！");
                is_disconnected = false;
            }

            // 执行异步平滑调速 (假设 100ms 的平滑渐变时间)
            DC_Motor_SetSpeedSmoothAsync(MOTOR_LEFT, cmd.left_speed, 100);
            DC_Motor_SetSpeedSmoothAsync(MOTOR_RIGHT, cmd.right_speed, 100);

        } else {
            // 看门狗咬人：超时未收到数据！
            if (!is_disconnected) {
                ESP_LOGW("MOTOR_TASK", "🚨 心跳超时！触发安全刹车！");
                is_disconnected = true;
                
                // 触发紧急平滑停止 (比如 300ms 停稳)
                DC_Motor_Stop(300); 
            }
        }
    }
}
