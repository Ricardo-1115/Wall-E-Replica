/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Console example — various system commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "command.h"
#include "sdkconfig.h"

#ifdef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#define WITH_TASKS_INFO 1
#endif

static const char *TAG = "cmd_system_common";

static void register_hello(void);
static void register_echo(void);

static void register_free(void);
static void register_heap(void);
static void register_version(void);
static void register_restart(void);
#if WITH_TASKS_INFO
static void register_tasks(void);
#endif

void register_system_common(void)
{
    register_hello();
    register_echo();

    register_free();
    register_heap();
    register_version();
    register_restart();
#if WITH_TASKS_INFO
    register_tasks();
#endif
}

/* 'hello' command */
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

static void register_hello(void){
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

/* 'echo' command */
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

static void register_echo(void){
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



/* 'version' command */
static int get_version(int argc, char **argv)
{
    const char *model;
    esp_chip_info_t info;
    uint32_t flash_size;
    esp_chip_info(&info);

    switch(info.model) {
        case CHIP_ESP32:
            model = "ESP32";
            break;
        case CHIP_ESP32S2:
            model = "ESP32-S2";
            break;
        case CHIP_ESP32S3:
            model = "ESP32-S3";
            break;
        case CHIP_ESP32C3:
            model = "ESP32-C3";
            break;
        case CHIP_ESP32H2:
            model = "ESP32-H2";
            break;
        case CHIP_ESP32C2:
            model = "ESP32-C2";
            break;
        default:
            model = "Unknown";
            break;
    }

    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return 1;
    }
    printf("IDF Version:%s\r\n", esp_get_idf_version());
    printf("Chip info:\r\n");
    printf("\tmodel:%s\r\n", model);
    printf("\tcores:%d\r\n", info.cores);
    printf("\tfeature:%s%s%s%s%"PRIu32"%s\r\n",
           info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
           info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
           info.features & CHIP_FEATURE_BT ? "/BT" : "",
           info.features & CHIP_FEATURE_EMB_FLASH ? "/Embedded-Flash:" : "/External-Flash:",
           flash_size / (1024 * 1024), " MB");
    printf("\trevision number:%d\r\n", info.revision);
    return 0;
}

static void register_version(void)
{
    const esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Get version of chip and SDK",
        .hint = NULL,
        .func = &get_version,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'restart' command restarts the program */

static int restart(int argc, char **argv)
{
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}

static void register_restart(void)
{
    const esp_console_cmd_t cmd = {
        .command = "restart",
        .help = "Software reset of the chip",
        .hint = NULL,
        .func = &restart,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'free' command prints available heap memory */

static int free_mem(int argc, char **argv)
{
    printf("%"PRIu32"\n", esp_get_free_heap_size());
    return 0;
}

static void register_free(void)
{
    const esp_console_cmd_t cmd = {
        .command = "free",
        .help = "Get the current size of free heap memory",
        .hint = NULL,
        .func = &free_mem,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/* 'heap' command prints minumum heap size */
static int heap_size(int argc, char **argv)
{
    uint32_t heap_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    printf("min heap size: %"PRIu32"\n", heap_size);
    return 0;
}

static void register_heap(void)
{
    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "Get minimum size of free heap memory that was available during program execution",
        .hint = NULL,
        .func = &heap_size,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&heap_cmd) );

}

/** 'tasks' command prints the list of tasks and related information */
#if WITH_TASKS_INFO

static int tasks_info(int argc, char **argv)
{
    const size_t bytes_per_task = 40; /* see vTaskList description */
    char *task_list_buffer = malloc(uxTaskGetNumberOfTasks() * bytes_per_task);
    if (task_list_buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate buffer for vTaskList output");
        return 1;
    }
    fputs("Task Name\tStatus\tPrio\tHWM\tTask#", stdout);
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
    fputs("\tAffinity", stdout);
#endif
    fputs("\n", stdout);
    vTaskList(task_list_buffer);
    fputs(task_list_buffer, stdout);
    free(task_list_buffer);
    return 0;
}

static void register_tasks(void)
{
    const esp_console_cmd_t cmd = {
        .command = "tasks",
        .help = "Get information about running tasks",
        .hint = NULL,
        .func = &tasks_info,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

#endif // WITH_TASKS_INFO


