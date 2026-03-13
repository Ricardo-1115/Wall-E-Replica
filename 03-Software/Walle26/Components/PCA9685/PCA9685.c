#include "PCA9685.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static i2c_master_bus_handle_t bus_handle = NULL;     // i2c总线句柄
static i2c_master_dev_handle_t dev_handle = NULL;     // i2c设备句柄

static void servo_fade_task(void *pvParameters);

static void i2c_pca9685_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_16,
        .scl_io_num = GPIO_NUM_15,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = 1,
        }
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA_Addr,
        .scl_speed_hz = 100000,
    };
    
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &device_config, &dev_handle));
}

static esp_err_t i2c_write_register(i2c_master_dev_handle_t i2c_dev, uint8_t reg_addr, 
                            const uint8_t *data, size_t data_len, int timeout_ms) {
    // 参数检查
    if (i2c_dev == NULL || data == NULL || data_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 使用栈内存，避免动态分配
    uint8_t write_buffer[data_len + 1];
    write_buffer[0] = reg_addr;
    memcpy(&write_buffer[1], data, data_len);
    
    return i2c_master_transmit(i2c_dev, write_buffer, sizeof(write_buffer), timeout_ms);
}

void pca9685_init(void)
{
    i2c_pca9685_init();

    // 初始化PCA9685
    uint8_t init_data = 0x00;
    ESP_ERROR_CHECK(i2c_write_register(dev_handle, PCA_Model, &init_data, 1, 100));

    // 启动伺服电机渐变任务
    xTaskCreatePinnedToCore(servo_fade_task, "servo_fade", 4096, NULL, 5, NULL, 1);
}

void pca9685_set_freq(float freq)
{
    uint8_t prescale, oldmode, newmode;
    double prescaleval;

    // 计算预分频值
    prescaleval = 25000000.0; // 25MHz
    prescaleval /= 4096.0;    // 12-bit
    prescaleval /= freq;
    prescaleval -= 1.0;
    prescale = (uint8_t)floor(prescaleval + 0.5f);

    // 读取当前模式
    uint8_t reg_addr = PCA_Model;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &oldmode, 1, 100));
    
    // 在MODE1中设置SLEEP位
    newmode = (oldmode & 0x7F) | 0x10; 
    
    // 进入睡眠模式
    ESP_ERROR_CHECK(i2c_write_register(dev_handle, PCA_Model, &newmode, 1, 100)); 
    
    // 设置预分频器
    ESP_ERROR_CHECK(i2c_write_register(dev_handle, PCA_Pre, &prescale, 1, 100));   
    
    // 重新复位
    ESP_ERROR_CHECK(i2c_write_register(dev_handle, PCA_Model, &oldmode, 1, 100)); 
    
    // 等待复位完成
    vTaskDelay(5 / portTICK_PERIOD_MS);

    // 设置自动递增模式
    uint8_t autoinc_mode = oldmode | 0xA1;
    ESP_ERROR_CHECK(i2c_write_register(dev_handle, PCA_Model, &autoinc_mode, 1, 100));
}


static void pca9685_set_pwm(uint8_t num, uint16_t on, uint16_t off)
{
    uint8_t buffer[4];
    buffer[0] = on & 0xFF;           // ON_L
    buffer[1] = (on >> 8) & 0x0F;    // ON_H (只取低4位)
    buffer[2] = off & 0xFF;          // OFF_L
    buffer[3] = (off >> 8) & 0x0F;   // OFF_H (只取低4位)

    ESP_ERROR_CHECK(i2c_write_register(dev_handle, LED0_ON_L + 4 * num, buffer, 4, 100));
}

void pca9685_set_angle(uint8_t Num, float Angle)
{
    uint32_t off = 0;
    
    Angle = Angle > 180.0f ? 180.0f : Angle; // 限制角度在0-180度范围内
    off = (uint32_t)(SERVO_MIN_TICK + (Angle * (SERVO_MAX_TICK - SERVO_MIN_TICK)) / 180.0f); // 计算对应的OFF值

    pca9685_set_pwm(Num, 0, off);
}



typedef struct {
    float current_angle;      // 当前角度
    float target_angle;       // 目标角度
    float step_per_tick;      // 每个tick(20ms)的角度变化量
}ServoState;

static ServoState servos[16] = {
    [0 ... 15] = {.current_angle = 90.0f, .target_angle = 90.0f, .step_per_tick = 0.0f} // 初始化所有舵机状态
};

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
