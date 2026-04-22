#include "Servo_app.h"

QueueHandle_t servo_cmd_queue = NULL;

static const char *TAG = "SERVO_APP";

typedef struct {
    float current_angle;      // 当前角度
    float target_angle;       // 目标角度
    float step_per_tick;      // 每个tick(20ms)的角度变化量
}ServoState;

static ServoState servos[16] = {
    [0 ... 15] = {.current_angle = 90.0f, .target_angle = 90.0f, .step_per_tick = 0.0f} // 初始化所有舵机状态
};

JointConfig walle_joint[7] = {
    [0 ... 6] = {.min_angle = 90.0f, .max_angle = 90.0f, .reverse = false}
};

static void servo_fade_task(void *pvParameters);
static void servo_control_task(void *pvParameters);

// Hardware initialization for PCA9685 on a shared I2C bus.
// This hides PCA9685-specific details from main.c.
esp_err_t servo_hw_init(i2c_master_bus_handle_t bus_handle)
{
    ESP_LOGI(TAG, "初始化 PCA9685 硬件...");

    esp_err_t err = pca9685_init_with_bus(bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685 硬件初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    pca9685_set_freq(50);
    ESP_LOGI(TAG, "PCA9685 硬件初始化成功");
    return ESP_OK;
}

// Starts servo application tasks and queue. PCA9685 hardware initialization is performed by main.c before calling this function.
void servo_app_init(void)
{
    // 1. 创建队列 (深度为2即可，只保留最新指令防积压)
    servo_cmd_queue = xQueueCreate(2, sizeof(servo_cmd_t));
    if (servo_cmd_queue == NULL) {
        ESP_LOGE(TAG, "舵机命令队列创建失败");
        return;
    }

    load_joint_config_from_nvs();

    // 2. 启动原有的舵机平滑渐变底层任务 (Core 1)
    xTaskCreatePinnedToCore(servo_fade_task, "servo_fade", 4096, NULL, 5, NULL, 1);

    // 3. 启动用于处理队列业务逻辑的任务 (Core 1)
    xTaskCreatePinnedToCore(servo_control_task, "servo_logic", 4096, NULL, 6, NULL, 1);
}


// 参数：通道号, 目标角度, 完成该动作期望的时间(毫秒)
// 供上层直接调用的函数，内部会计算每个tick需要变化的角度（相当于动画的速度），然后在servo_fade_task中逐步更新当前角度并发送到PCA9685
void walle_move_servo(uint8_t channel, float target_angle, uint32_t duration_ms){
    if(channel > 15 || target_angle < 0.0f || target_angle > 180.0f){
        ESP_LOGE("walle_move_servo", "Invalid channel or angle. Channel must be 0-15, angle must be 0-180.");
        return;
    }

    float diff = target_angle - servos[channel].current_angle;

    if(duration_ms < 20){
        // if duration is less than one tick, move immediately
        servos[channel].step_per_tick = fabs(diff);
    }
    else{
        // calculate step per tick based on duration
        float steps = duration_ms / 20.0f; // 每20ms一个tick
        servos[channel].step_per_tick = fabs(diff) / steps;
    }

    servos[channel].target_angle = target_angle;
}



static void servo_fade_task(void *pvParameters){
    while(1){
        for(int i = 0; i < 16; ++i){
            if(servos[i].current_angle != servos[i].target_angle){
                float diff = servos[i].target_angle - servos[i].current_angle;
                float step = servos[i].step_per_tick;

                if(fabs(diff) <= step){
                    // if the remaining difference is less than one step, move directly to target
                    servos[i].current_angle = servos[i].target_angle;
                }
                else if(diff > 0){
                    // move up
                    servos[i].current_angle += step;
                }
                else{
                    // move down
                    servos[i].current_angle -= step;
                }

                pca9685_set_angle(i, servos[i].current_angle);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // 每20ms更新一次
    }
}

void walle_set_joint_move(uint8_t channel, float percentage, uint32_t duration_ms){
    if(channel > 15 || percentage < 0 || percentage > 100){
        ESP_LOGE(TAG, "Invalid parameters");
        return;
    }

    JointConfig cfg = walle_joint[channel];
    float joint_angle_target = 90.0f;
    float joint_angle_range = 0.0f;

    if(cfg.reverse == true){
        joint_angle_range = cfg.max_angle - cfg.min_angle;
        joint_angle_target = (cfg.min_angle + percentage / 100.0f * joint_angle_range);
    }
    else{
        joint_angle_range = cfg.max_angle - cfg.min_angle;
        joint_angle_target = (cfg.max_angle - percentage / 100.0f * joint_angle_range);
    }

    walle_move_servo(channel, joint_angle_target, duration_ms);
}

// 处理来自 WiFi/自动脚本 队列的数据任务
static void servo_control_task(void *pvParameters) {
    servo_cmd_t cmd;
    while(1) {
        // 阻塞等待队列数据
        if (xQueueReceive(servo_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            // 收到指令后，循环遍历 7 个关节，将百分比和特定的持续时间设置下去
            for (uint8_t i = 0; i < 7; i++) {
                walle_set_joint_move(i, cmd.percentages[i], cmd.duration_ms); 
            }
        }
    }
}
