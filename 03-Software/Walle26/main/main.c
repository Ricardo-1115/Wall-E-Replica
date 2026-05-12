/*
 * 项目名称：基于 ESP32 的移动机器人无线图传系统
 * Copyright (C) 2026 gclv
 * * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 
 */
#include <stdio.h>
#include <string.h>
#include <u8g2.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "command.h"
#include "DC_Motor.h"
#include "Servo_app.h"
#include "wifi.h"
#include "DFPlayerMini.h"
#include "esp32_hw_i2c.h"
#include "display.h"
#include "battery.h"
#include "animation_engine.h"



/*
 * We warn if a secondary serial console is enabled. A secondary serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

static const char* TAG = "example";
#define PROMPT_STR CONFIG_IDF_TARGET


/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */
#if CONFIG_CONSOLE_STORE_HISTORY

#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"

static void initialize_filesystem(void)
{
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_PATH, "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}
#endif // CONFIG_STORE_HISTORY

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// ============ 全局 I2C 总线管理 ============
static i2c_master_bus_handle_t g_i2c_bus_handle = NULL;

// I2C 总线初始化（一主多从）
static esp_err_t init_i2c_bus(void)
{
    if (g_i2c_bus_handle != NULL) {
        ESP_LOGI(TAG, "I2C 总线已初始化");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_4,
        .scl_io_num = GPIO_NUM_5,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 1,
        .flags.allow_pd = 0,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &g_i2c_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线创建失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C 总线初始化成功 (I2C_NUM_0, SDA=GPIO4, SCL=GPIO5, 400kHz)");
    return ESP_OK;
}

// ============ U8G2 OLED 初始化 ============
u8g2_t u8g2;
static u8g2_esp32_i2c_ctx_t g_oled_i2c_ctx = {
    .cfg = U8G2_ESP32_I2C_CONFIG_DEFAULT(),
};

static esp_err_t __attribute__((unused)) init_oled_display(void)
{
    ESP_LOGI(TAG, "初始化 U8G2 OLED...");

    // 使用全局 I2C 总线初始化 U8G2
    esp_err_t err = u8g2_esp32_i2c_init_with_bus(&g_oled_i2c_ctx, g_i2c_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "U8G2 I2C 上下文初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    // 设置 U8G2 外观
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
        u8x8_byte_esp32_hw_i2c, u8x8_gpio_and_delay_esp32_i2c);

    // 初始化显示屏
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); // 关闭省电模式

    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);

    ESP_LOGI(TAG, "U8G2 OLED 初始化成功");
    return ESP_OK;
}

// ============ 舵机硬件初始化 ============
static esp_err_t init_servo_hardware(void)
{
    ESP_LOGI(TAG, "初始化舵机硬件...");

    esp_err_t err = Servo_hw_Init(g_i2c_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "舵机硬件初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "舵机硬件初始化成功");
    return ESP_OK;
}

// ============ I2C 设备初始化层 ============
static void init_i2c_devices(void)
{
    if (init_i2c_bus() != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线初始化失败，程序退出");
        return;
    }

    if (init_oled_display() != ESP_OK) {
        ESP_LOGE(TAG, "OLED 初始化失败");
        // 继续运行，不影响其他功能
    }

    if (init_servo_hardware() != ESP_OK) {
        ESP_LOGE(TAG, "舵机硬件初始化失败");
        // 继续运行，不影响其他功能
    }

    ESP_LOGI(TAG, "所有 I2C 设备初始化完成");
}

void app_main(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH;

    initialize_nvs();
    // 初始化 I2C 设备和总线
    init_i2c_devices();

    // 初始化电池电压监测
    battery_init();
    
    DFPlayerMini_Init();
    DFPlayerMini_SetVolume(15);     /* 统一音量 */
    wifi_init();
    init_camera();

    DC_Motor_Init();
    motor_cmd_queue = xQueueCreate(1, sizeof(motor_cmd_t));
    if (motor_cmd_queue == NULL) {
        ESP_LOGE("MAIN", "队列创建失败！");
        return;
    }

    // 创建独立运行的电机控制任务
    xTaskCreatePinnedToCore(motor_control_task, "motor_ctrl_task", 4096, NULL, 15, NULL, 1);
    // 初始化舵机控制系统
    Servo_app_Init();
    // 初始化动画引擎
    anim_engine_init();
    anim_engine_enable_idle(true);   // 默认开启空闲微表情

#if CONFIG_CONSOLE_STORE_HISTORY
    initialize_filesystem();
    repl_config.history_save_path = HISTORY_PATH;
    ESP_LOGI(TAG, "Command history enabled");
#else
    ESP_LOGI(TAG, "Command history disabled");
#endif

    /* Register commands */
    esp_console_register_help_command();
    register_system_common();

    register_nvs();

    register_motor_set();
    register_motor_stop();
    register_servo();
    register_servo_key();
    register_servo_calib();
    register_dfplayer_play_folder();
    register_anim_debug();
    register_anim_idle();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

#else
#error Unsupported console type
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    // 主循环：每 2 秒刷新 OLED 显示电池电量
    while(1) {
        uint32_t mv = battery_get_voltage_mv();
        uint8_t pct = battery_get_percentage();
        display_battery_show(pct, mv);  /* 接上 OLED 后取消注释 */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

