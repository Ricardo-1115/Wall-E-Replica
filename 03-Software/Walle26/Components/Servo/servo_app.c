#include "Servo_app.h"
#include "nvs.h"

QueueHandle_t servo_cmd_queue = NULL;

static const char *TAG = "SERVO_APP";

typedef struct {
    float current_angle;      // 当前角度
    float target_angle;       // 目标角度
    float step_per_tick;      // 每个tick(20ms)的角度变化量
}ServoState;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
static ServoState servos[16] = {
    [0 ... 15] = {.current_angle = 90.0f, .target_angle = 90.0f, .step_per_tick = 0.0f},
    [3] = {.current_angle = 30.0f, .target_angle = 30.0f, .step_per_tick = 0.0f},
};
#pragma GCC diagnostic pop

JointConfig walle_joint[JOINT_COUNT] = {
    [0 ... (JOINT_COUNT - 1)] = {.min_angle = 90.0f, .max_angle = 90.0f, .reverse = false}

};

static const char *NVS_TAG = "NVS_JOINT";

void save_joint_config_to_nvs(void){
    nvs_handle_t my_handle;
    // Open
    ESP_ERROR_CHECK(nvs_open(NVS_TAG, NVS_READWRITE, &my_handle));

    // Write
    esp_err_t err = nvs_set_blob(my_handle, "walle_joint", walle_joint, sizeof(walle_joint));
    if(err != ESP_OK){
        ESP_LOGE(NVS_TAG, "保存 Blob 失败! 错误码: %s", esp_err_to_name(err));
    }
    else{
        err = nvs_commit(my_handle);
        if(err == ESP_OK){
            ESP_LOGI(NVS_TAG, "关节参数已成功写入NVS.");
        }
    }

    // Close
    nvs_close(my_handle);
}

void load_joint_config_from_nvs(void){
    nvs_handle_t my_handle;
    esp_err_t err;

    // 1. 打开 NVS, 模式为只读
    err = nvs_open(NVS_TAG, NVS_READONLY, &my_handle);
    if(err != ESP_OK){
        ESP_LOGW(NVS_TAG, "NVS尚未初始化或没有数据，将使用默认数据！");
        return;
    }

    size_t require_size = 0;
    // 2. 第一次调用nvs_get_blob：传入 NULL 获取保存在 NVS 的数据长度
    err = nvs_get_blob(my_handle, "walle_joint", NULL, &require_size);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(NVS_TAG, "读取 nvs 数据长度失败");
        nvs_close(my_handle);
        return;
    }

    // 3. 检查有没有找到数据， 并且数据长度是否与我们现在结构体数组大小一致
    if(require_size == sizeof(walle_joint)){
        // 4. 第二次调用 nvs_get_blob, 真正把数据读取到 walle_joint 数组中
        err = nvs_get_blob(my_handle, "walle_joint", walle_joint, &require_size);
        if(err == ESP_OK){
            ESP_LOGI(NVS_TAG, "成功从 NVS 加载了7个关节的配置参数");
        }
    }
    else if(require_size > 0){
        ESP_LOGE(NVS_TAG, "NVS 中的数据大小(%d)与当前结构体大小(%d)不匹配！放弃读取，使用默认值", require_size, sizeof(walle_joint));
    }
    else{
        ESP_LOGI(NVS_TAG, "NVS 中没有找到关节配置数据，使用代码中的默认初始化值。");
    }

    // 5. 关闭句柄
    nvs_close(my_handle);
}

static void servo_fade_task(void *pvParameters);
static void servo_control_task(void *pvParameters);

float get_servo_angle(uint8_t channel)
{
    if (channel > 15) return 90.0f;
    return servos[channel].current_angle;
}

// Hardware initialization for PCA9685 on a shared I2C bus.
// This hides PCA9685-specific details from main.c.
esp_err_t Servo_hw_Init(i2c_master_bus_handle_t bus_handle)
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
void Servo_app_Init(void)
{
    // 1. 创建队列 
    servo_cmd_queue = xQueueCreate(1, sizeof(servo_cmd_t));
    if (servo_cmd_queue == NULL) {
        ESP_LOGE(TAG, "舵机命令队列创建失败");
        return;
    }

    load_joint_config_from_nvs();

    // 2. 启动舵机平滑渐变任务 (Core 1, 优先级高保障 20ms 周期)
    xTaskCreatePinnedToCore(servo_fade_task, "servo_fade", 4096, NULL, 7, NULL, 1);

    // 3. 启动队列业务逻辑任务 (Core 1, 优先级低于 fade, 避免干扰定时)
    xTaskCreatePinnedToCore(servo_control_task, "servo_logic", 4096, NULL, 5, NULL, 1);
}


// 底层舵机接口：设置 PCA9685 指定通道的目标角度，duration_ms 控制缓动时长（毫秒）
void walle_servo_set_angle(uint8_t channel, float angle, uint32_t duration_ms){
    if(channel > 15 || angle < 0.0f || angle > 180.0f){
        ESP_LOGE("walle_servo_set_angle", "Invalid channel or angle. Channel must be 0-15, angle must be 0-180.");
        return;
    }

    float diff = angle - servos[channel].current_angle;

    if(duration_ms < 20){
        // 小于一个 tick 周期则直接跳转
        servos[channel].step_per_tick = fabs(diff);
    }
    else{
        // 根据时长计算每个 tick (20ms) 应转动的角度
        float steps = duration_ms / 20.0f;
        servos[channel].step_per_tick = fabs(diff) / steps;
    }

    servos[channel].target_angle = angle;
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

// 上层关节接口：将关节百分比 (0-100) 经 JointConfig 映射为绝对角度后调 walle_servo_set_angle
void walle_joint_move(uint8_t joint_id, float percentage, uint32_t duration_ms){
    if(joint_id >= JOINT_COUNT || percentage < 0 || percentage > 100){
        ESP_LOGE(TAG, "Invalid parameters: joint_id must be 0-%d, percentage must be 0-100", JOINT_COUNT - 1);
        return;
    }

    JointConfig cfg = walle_joint[joint_id];
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

    walle_servo_set_angle(joint_id, joint_angle_target, duration_ms);
}

// 处理来自 WiFi/自动脚本 队列的数据任务
static void servo_control_task(void *pvParameters) {
    servo_cmd_t cmd;
    while(1) {
        if (xQueueReceive(servo_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            for (uint8_t i = 0; i < JOINT_COUNT; i++) {
                walle_joint_move(i, cmd.percentages[i], cmd.duration_ms);
            }
        }
    }
}
