#include "Servo_app.h"
#include "nvs.h"

QueueHandle_t servo_cmd_queue = NULL;

static const char *TAG = "SERVO_APP";

typedef struct {
    float current_angle;      // 当前角度
    float target_angle;       // 目标角度
    float start_angle;        // 本次运动起始角度
    TickType_t start_tick;    // 本次运动起始 tick
    uint32_t duration_ticks;  // 本次运动起始 tick 数
}ServoState;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
static ServoState servos[16] = {
    [0 ... 15] = {.current_angle = 90.0f, .target_angle = 90.0f},
    [3] = {.current_angle = 30.0f, .target_angle = 30.0f},
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

static float ease_in_out_quad(float t){
    if(t <= 0.0f) return 0.0f;
    if(t >= 1.0f) return 1.0f;
    if(t < 0.5f) return 2.0f * t * t;
    return -1.0f + (4.0f - 2.0f * t) * t;
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

    if(servos[channel].current_angle == angle) return;

    if(duration_ms < portTICK_PERIOD_MS)
        duration_ms = portTICK_PERIOD_MS;

    servos[channel].start_angle    = servos[channel].current_angle;
    servos[channel].target_angle   = angle;
    servos[channel].start_tick     = xTaskGetTickCount();
    servos[channel].duration_ticks = pdMS_TO_TICKS(duration_ms);
}



static void servo_fade_task(void *pvParameters){
    while(1){
        TickType_t now = xTaskGetTickCount();
        for(int i = 0; i < 16; ++i){
            if(servos[i].current_angle == servos[i].target_angle)
                continue;

            TickType_t elapsed = now - servos[i].start_tick;

            if(elapsed >= servos[i].duration_ticks){
                servos[i].current_angle = servos[i].target_angle;
            } else {
                float t = (float)elapsed / (float)servos[i].duration_ticks;
                t = ease_in_out_quad(t);
                servos[i].current_angle = servos[i].start_angle
                    + (servos[i].target_angle - servos[i].start_angle) * t;
            }
            pca9685_set_angle(i, servos[i].current_angle);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// 上层关节接口：将关节百分比 (0-100) 经 JointConfig 映射为绝对角度后调 walle_servo_set_angle
void walle_joint_move(uint8_t joint_id, float percentage, uint32_t duration_ms){
    if(joint_id >= JOINT_COUNT || percentage < 0 || percentage > 100){
        ESP_LOGE(TAG, "Invalid parameters: joint_id must be 0-%d, percentage must be 0-100", JOINT_COUNT - 1);
        return;
    }

    JointConfig cfg = walle_joint[joint_id];
    float joint_angle_range = cfg.max_angle - cfg.min_angle;
    float joint_angle_target;

    if(cfg.reverse == true){
        joint_angle_target = cfg.min_angle + percentage / 100.0f * joint_angle_range;
    }
    else{
        joint_angle_target = cfg.max_angle - percentage / 100.0f * joint_angle_range;
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
