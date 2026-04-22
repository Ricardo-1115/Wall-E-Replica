# I2C 一主多从集成方案

## 概述

本方案实现了 ESP32 中 **I2C 一主多从** 的集中管理，在**同一条 I2C 总线**（I2C_NUM_0）上连接多个从设备：

- **PCA9685**（PWM/舵机控制器）- 地址 `0x40`
- **SSD1306 OLED**（显示屏）- 地址 `0x3C`

### 硬件连接

- **I2C 引脚**：SDA = GPIO38，SCL = GPIO14
- **时钟速率**：400 kHz
- **通信协议**：I2C Master with multiple I2C Slave devices

---

## 系统架构

### 之前的问题

```
❌ PCA9685.c        创建自己的 I2C 总线（重复初始化）
❌ main.c (U8G2)    创建另一条 I2C 总线
→ 结果：两条独立的 I2C 总线，浪费资源，可能冲突
```

### 改进后的架构

```
✅ main.c
   └─ init_i2c_bus()           ← 创建唯一的 I2C 总线
      |
      ├─ init_servo_hardware()  ← 使用共享总线
      |  └─ servo_hw_init(bus_handle)
      |
      └─ init_oled_display()   ← 使用共享总线
         └─ u8g2_esp32_i2c_init_with_bus(ctx, bus_handle)
```

---

## 修改详情

### 1. **PCA9685.c** - 添加支持外部 bus_handle

**新增函数：**

```c
esp_err_t pca9685_init_with_bus(i2c_master_bus_handle_t bus_handle)
```

**原有函数保留（向后兼容）：**

```c
esp_err_t pca9685_init(void)  // 仍然自创 I2C 总线
```

### 2. **Servo.h** - 暴露新的初始化 API

```c
// 原始初始化（自动创建 I2C 总线）
esp_err_t pca9685_init(void);

// 新增：使用外部 I2C 总线初始化（支持一主多从）
esp_err_t pca9685_init_with_bus(i2c_master_bus_handle_t bus_handle);
```

### 3. **esp32_hw_i2c.h/.c** - U8G2 OLED 驱动升级

**新增函数：**

```c
esp_err_t u8g2_esp32_i2c_init_with_bus(
    u8g2_esp32_i2c_ctx_t *ctx, 
    i2c_master_bus_handle_t bus_handle
)
```

**功能：** 不创建新的 I2C 总线，而是使用外部提供的总线句柄

### 4. **main.c** - 集中管理所有 I2C 设备

**四个初始化层次：**

#### `init_i2c_bus()` - 初始化唯一的 I2C 总线

```c
static esp_err_t init_i2c_bus(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_38,
        .scl_io_num = GPIO_NUM_14,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };

    return i2c_new_master_bus(&bus_config, &g_i2c_bus_handle);
}
```

#### `init_servo_hardware()` - 初始化舵机硬件

```c
static esp_err_t init_servo_hardware(void)
{
    esp_err_t err = servo_hw_init(g_i2c_bus_handle);
    return err;
}
```

#### `init_oled_display()` - 初始化 OLED 显示屏

```c
static esp_err_t init_oled_display(void)
{
    u8g2_esp32_i2c_ctx_t i2c_ctx = {
        .cfg = U8G2_ESP32_I2C_CONFIG_DEFAULT(),
    };

    esp_err_t err = u8g2_esp32_i2c_init_with_bus(&i2c_ctx, g_i2c_bus_handle);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
        u8x8_byte_esp32_hw_i2c, u8x8_gpio_and_delay_esp32_i2c);

    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    return err;
}
```

---

## 新的组件边界

### `servo_hw_init()` 和 `servo_app_init()` 的职责

- `servo_hw_init(i2c_master_bus_handle_t bus_handle)`
  - 负责 PCA9685 硬件初始化与频率设置
  - 属于“硬件初始化层”
- `servo_app_init()`
  - 负责舵机命令队列、任务启动与业务逻辑
  - 不再负责硬件初始化

---

## 使用示例

### 基本初始化流程

```c
void app_main(void)
{
    // 1. 初始化唯一的 I2C 总线
    if (init_i2c_bus() != ESP_OK) {
        ESP_LOGE(TAG, "I2C 初始化失败");
        return;
    }

    // 2. 在同一总线上添加 OLED 设备
    if (init_oled_display() != ESP_OK) {
        ESP_LOGW(TAG, "OLED 初始化失败，但继续运行");
    }

    // 3. 在同一总线上添加舵机控制器
    if (init_servo_hardware() != ESP_OK) {
        ESP_LOGW(TAG, "舵机硬件初始化失败，但继续运行");
    }

    // 正常使用
    pca9685_set_angle(0, 90.0f);  // 舵机动作
    u8g2_DrawStr(&u8g2, 0, 10, "Hello");  // 显示文字
}
```

---

## 关键优势

| 方面           | 之前         | 改进后         |
| ------------ | ---------- | ----------- |
| **I2C 总线数量** | 2 条（浪费资源）  | 1 条（共享）     |
| **代码重复**     | 每个设备都初始化总线 | 只初始化一次      |
| **灵活性**      | 低（硬编码总线配置） | 高（参数化）      |
| **扩展性**      | 差（难以添加新设备） | 好（在同一总线上添加） |
| **资源占用**     | 更多         | 更少          |

---

## 添加新 I2C 设备的步骤

假设要添加第三个 I2C 设备（如温度传感器，地址 `0x48`）：

1. **编写驱动程序**（支持外部 bus_handle）：
   
   ```c
   esp_err_t temp_sensor_init_with_bus(i2c_master_bus_handle_t bus_handle)
   {
       // 将 bus_handle 传给驱动初始化函数
   }
   ```

2. **在 main.c 中添加初始化函数**：
   
   ```c
   static esp_err_t init_temp_sensor(void)
   {
       return temp_sensor_init_with_bus(g_i2c_bus_handle);
   }
   ```

3. **在 app_main 中调用**：
   
   ```c
   init_temp_sensor();
   ```

无需修改 I2C 总线配置，直接添加即可！

---

## 故障排除

### 问题：OLED 无法显示

**检查项：**

- OLED 地址是否为 `0x3C`（配置中 `.dev_addr_7bit = 0x3C`）
- SDA/SCL 引脚是否正确（GPIO38/GPIO14）
- I2C 总线是否正确初始化

### 问题：舵机不动作

**检查项：**

- PCA9685 地址是否为 `0x40`（头文件中 `#define PCA_Addr 0x40`）
- PCA9685_init_with_bus 是否成功返回
- `pca9685_set_freq(50)` 是否被调用

### 问题：两个设备同时出错

**检查项：**

- I2C 总线是否初始化成功（检查返回值）
- I2C 引脚是否被正确配置
- ESP32 是否启用了内部上拉电阻

---

## 参考资源

- **ESP32 I2C Master API**：https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html
- **u8g2 库**：https://github.com/olikraus/u8g2
- **PCA9685 数据手册**：https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf

---

## 总结

✅ **一条 I2C 总线** - 共享 SDA/SCL 引脚  
✅ **多个从设备** - 使用不同地址（0x40, 0x3C, ...）  
✅ **集中管理** - 在 main.c 中管理所有初始化  
✅ **易于扩展** - 添加新设备只需实现 `xxx_init_with_bus(bus_handle)`  
✅ **向后兼容** - 原有 `pca9685_init()` 仍可单独使用，新的 `servo_hw_init()` 供共享 I2C 总线时使用
