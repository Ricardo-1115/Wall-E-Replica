#include "PCA9685.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static i2c_master_bus_handle_t bus_handle = NULL;     // i2c总线句柄
static i2c_master_dev_handle_t dev_handle = NULL;     // i2c设备句柄




// 使用外部 I2C 总线初始化（支持一主多从）
static esp_err_t i2c_pca9685_init_with_bus(i2c_master_bus_handle_t external_bus_handle)
{
    if (external_bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bus_handle = external_bus_handle;
    
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA_Addr,
        .scl_speed_hz = 400000,
    };
    
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &device_config, &dev_handle);
    if (err != ESP_OK) {
        dev_handle = NULL;
        return err;
    }

    return ESP_OK;
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


// 使用外部 I2C 总线初始化
esp_err_t pca9685_init_with_bus(i2c_master_bus_handle_t bus_handle)
{
    esp_err_t err = i2c_pca9685_init_with_bus(bus_handle);
    if (err != ESP_OK) {
        return err;
    }

    // 初始化PCA9685
    uint8_t init_data = 0x00;
    ESP_ERROR_CHECK(i2c_write_register(dev_handle, PCA_Model, &init_data, 1, 100));

    return ESP_OK;
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



