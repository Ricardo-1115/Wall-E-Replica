#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "DC_Motor.h"

static struct {
    struct arg_end *end;
}hello_args;

static int cmd_hello(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **) &hello_args);
    if(nerrors != 0){
        arg_print_errors(stderr, hello_args.end, argv[0]);
        return 1;
    }
    
    // 1. 灵魂注入：BnL 启动画面 
   printf("\n\e[0;33m"); // 设置终端字体颜色为黄色
    printf("  ____                 _   _   _                          \n");
    printf(" |  _ \\               | \\ | | | |                         \n");
    printf(" | |_) |_   _ _   _   |  \\| | | |     __ _ _ __ __ _  ___ \n");
    printf(" |  _ <| | | | | | |  | . ` | | |    / _` | '__/ _` |/ _ \\\n");
    printf(" | |_) | |_| | |_| |  | |\\  | | |___| (_| | | | (_| |  __/\n");
    printf(" |____/ \\__,_|\\__, |  |_| \\_| |______\\__,_|_|  \\__, |\\___|\n");
    printf("               __/ |                            __/ |     \n");
    printf("              |___/                            |___/      \n");
    printf("     Buy n Large Corporation OS v2.0                      \n");
    printf("\e[0m\n"); // 恢复默认颜色

    printf("      [o_o]   <- W.A.L.L.-E UNIT ACTIVE\n");
    printf("     /|___|\\  \n");
    printf("      d   b   \n\n");

    // 2. 获取芯片、Flash 和系统运行时间信息
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    if(esp_flash_get_physical_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }
    
    // 计算 Uptime
    uint64_t uptime_sec = esp_timer_get_time() / 1000000;
    uint32_t h = uptime_sec / 3600;
    uint32_t m = (uptime_sec % 3600) / 60;
    uint32_t s = uptime_sec % 60;

    // 3. 打印系统诊断报告 
    printf("\e[0;36m=== BnL UNIT W.A.L.L.-E : SYSTEM DIAGNOSTICS ===\e[0m\n");
    
    // 基础硬件层
    printf("[\e[0;32mOK\e[0m] Core Brain   : ESP32-S3 (Cores: %d, Rev: %d)\n", chip_info.cores, chip_info.revision);
    printf("[\e[0;32mOK\e[0m] Firmware OS  : ESP-IDF %s\n", esp_get_idf_version());
    printf("[\e[0;32mOK\e[0m] Build Time   : %s %s\n", __DATE__, __TIME__);
    printf("[\e[0;32mOK\e[0m] Memory Status: %" PRIu32 " Bytes Free / Min: %" PRIu32 "\n", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    printf("[\e[0;32mOK\e[0m] Flash Drive  : %" PRIu32 " MB %s\n", flash_size / (1024 * 1024), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External");
    printf("[\e[0;32mOK\e[0m] Sys Uptime   : %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 "\n", h, m, s);
    
    printf("------------------------------------------------\n");
    printf("\e[0;35m--- PERIPHERAL & SUBSYSTEM STATUS ---\e[0m\n");
    
    // 外设与生态层 
    printf("[\e[0;32mON\e[0m] Motor Driver : LEDC Async Fade (4kHz, 13-bit)\n");
    printf("[\e[0;33mWARN\e[0m] Solar Array  : Disconnected [AFE Pending]\n");
    printf("[\e[0;33mWARN\e[0m] Chassis Track: Offline [Mech Assembly in Progress]\n");
    printf("[\e[0;31mFAIL\e[0m] Earth Ecology: Critical [Directive: Clean Up]\n");
    
    printf("------------------------------------------------\n");
    
    printf("Primary Dir.   : \e[1;37mM-O-C-C (Make Our Code Clean)\e[0m\n"); 
    printf("\e[0;36m================================================\e[0m\n\n");

    ESP_LOGI("WALL-E", "Boot sequence diagnostics complete. Ready for input.\n");
    return 0;
}

void register_hello(void){
    hello_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "hello",
        .help = "Run WALL-E system diagnostics and status report",
        .hint = NULL,
        .func = &cmd_hello,
        .argtable = &hello_args
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}


static struct {
    struct arg_str *messege;
    struct arg_end *end;
}echo_args;

static int cmd_echo(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **) &echo_args);
    if(nerrors != 0){
        arg_print_errors(stderr,echo_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI("CMD", "%s\n", echo_args.messege->sval[0]);
    return 0;
}

void register_echo(void){
    echo_args.messege = arg_str1(NULL, NULL, "<messege>", "Messege to echo");
    echo_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "echo",
        .help = "Echo the input messege",
        .hint = NULL,
        .func = &cmd_echo,
        .argtable = &echo_args
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_int *motor_num;
    struct arg_int *motor_speed;
    struct arg_int *duration;
    struct arg_end *end;
}motor_set_args;

static int cmd_motor_set(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **) &motor_set_args);
    if(nerrors != 0){
        arg_print_errors(stderr, motor_set_args.end, argv[0]);
        return 1;
    }
    int motor_num = motor_set_args.motor_num->ival[0];
    int motor_speed = motor_set_args.motor_speed->ival[0];
    uint32_t duration = 0; // default to 0 for immediate speed change
    if(motor_set_args.duration->count > 0){
        duration = (uint32_t)motor_set_args.duration->ival[0];
    }
    if(motor_num < 0 || motor_num > 1) {
        ESP_LOGE("MOTOR_CMD", "Invalid motor number: %d. Must be 0 (left) or 1 (right).\n", motor_num);
        return 1;
    }
    if(motor_speed < -100 || motor_speed > 100){
        ESP_LOGE("MOTOR_CMD", "Invalid motor speed: %d. Must be between -100 and 100.\n",motor_speed);
        return 1;
    }
    ESP_LOGI("MOTOR_CMD", "Setting motor %d speed to %d for %lu ms\n", motor_num, motor_speed, duration);


    // esp_err_t err = DC_Motor_SetSpeedSmooth((motor_num == 1) ? MOTOR_RIGHT : MOTOR_LEFT, (int8_t)motor_speed, duration);
    esp_err_t err = DC_Motor_SetSpeedSmoothAsync((motor_num == 1) ? MOTOR_RIGHT : MOTOR_LEFT, (int8_t)motor_speed, duration);
    if (err != ESP_OK) {
        ESP_LOGE("MOTOR_CMD", "Failed to set motor speed (err=%d)", err);
        return 1;
    }
    return 0;
}

void register_motor_set(void){
    motor_set_args.motor_num = arg_int1(NULL, NULL, "<motor_num>", "Motor number (0 for left, 1 for right)");
    motor_set_args.motor_speed = arg_int1("v", NULL, "<motor_speed>", "Motor speed (-100 to 100)");
    motor_set_args.duration = arg_int0("t", NULL, "<duration_ms>", "Duration for smooth speed change (ms, optional), default 0 for immediate change)");
    motor_set_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "motor_set",
        .help = "Set the speed of the specified motor. Usage: motor set <motor_num> <motor_speed> [duration_ms]",
        .hint = NULL,
        .func = &cmd_motor_set,
        .argtable = &motor_set_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_int *duration_ms;
    struct arg_lit *info;
    struct arg_end *end;
}motor_stop_args;

static int cmd_motor_stop(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **) &motor_stop_args);
    if(nerrors != 0){
        arg_print_errors(stderr, motor_stop_args.end, argv[0]);
        return 1;
    }
    int duration_ms = 300; // default duration for smooth stop
    if(motor_stop_args.duration_ms->count > 0){
        duration_ms = motor_stop_args.duration_ms->ival[0];
    }
    bool info = motor_stop_args.info->count > 0;

    if(info){
        ESP_LOGI("MOTOR_CMD", "Currently Left Motor Speed: %d, Right Motor Speed: %d\n", current_speed[MOTOR_LEFT], current_speed[MOTOR_RIGHT]);
    }
    esp_err_t err = DC_Motor_Stop(duration_ms);
    if(err != ESP_OK){
        ESP_LOGE("MOTOR_CMD", "Failed to stop motor (err=%d)!!!", err);
        return 1;
    }
    ESP_LOGI("MOTOR_CMD", "Motor stopped successfully.\n");
    return 0;
}

void register_motor_stop(void){
    motor_stop_args.duration_ms = arg_int0(NULL, NULL, "<duration_ms>", "Duration for smooth speed change (ms), default 300ms)");
    motor_stop_args.info = arg_lit0(NULL, "info", "Print current motor speeds before stopping");
    motor_stop_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "motor_stop",
        .help = "Stop the motor with optional smooth fading. Usage: motor_stop [duration_ms] [--info]",
        .hint = NULL,
        .func = &cmd_motor_stop,
        .argtable = &motor_stop_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
