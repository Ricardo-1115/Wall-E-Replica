/**
 * @file motor_ripple_test.c
 * @brief 电源纹波测试 CLI 命令
 *
 * 设计用途：配合示波器测量电机运行工况下的电源纹波和地弹噪声。
 * 对应论文第 5 章 5.1.2（稳态纹波）和 5.1.3（启停瞬态地弹）。
 *
 * ====== 测试场景 ======
 *
 * 1. 稳态纹波测量 (5.1.2)
 *    ripple_run <left%> <right%> [-t <s>]
 *    让电机以指定速度持续运转，示波器测量 DC-DC 输出纹波。
 *    论文中测试条件：50% 占空比连续运转。
 *
 * 2. 启停瞬态地弹测量 (5.1.3)
 *    ripple_cycle [-i <s>] [-c <n>]
 *    自动循环：0 → +100 → 0 → -100 → 0 ...
 *    在电机启停瞬间用示波器单次触发捕捉地平面噪声波形。
 *
 * 3. 自定义脉冲
 *    ripple_pulse <speed> [-t <ms>]
 *    单次脉冲：从静止加速到指定速度，保持后减速停止。
 *
 * 注意：通过 motor_cmd_queue 发送指令而非直接调 DC_Motor_SetSpeed，
 * 以保持 motor_control_task 的心跳机制不超时。
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DC_Motor.h"

static const char *TAG = "RIPPLE_TEST";

/* 通过队列发送双电机指令，保持心跳不超时 */
static void send_motor_cmd(int8_t left, int8_t right)
{
    motor_cmd_t cmd = { .left_speed = left, .right_speed = right };
    if (motor_cmd_queue != NULL) {
        xQueueOverwrite(motor_cmd_queue, &cmd);
    }
}

/* ================================================================== */
/*  Subcommand: ripple_run — 双电机持续运转                              */
/* ================================================================== */
static struct {
    struct arg_int *left;
    struct arg_int *right;
    struct arg_int *duration_s;
    struct arg_end *end;
} run_args;

static int cmd_ripple_run(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&run_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, run_args.end, argv[0]);
        return 1;
    }

    int8_t left = (int8_t)run_args.left->ival[0];
    int8_t right = (int8_t)run_args.right->ival[0];

    if (left < -100 || left > 100 || right < -100 || right > 100) {
        ESP_LOGE(TAG, "速度范围: -100 ~ 100");
        return 1;
    }

    uint32_t duration_s = 0;
    if (run_args.duration_s->count > 0) {
        duration_s = (uint32_t)run_args.duration_s->ival[0];
    }

    ESP_LOGI(TAG, "=== 纹波测试: RUN ===");
    ESP_LOGI(TAG, "左=%d%%, 右=%d%%, 持续=%lus", left, right, duration_s);
    ESP_LOGI(TAG, "示波器: AC 耦合, 20mV/div, 时基 1ms/div");

    send_motor_cmd(left, right);

    if (duration_s > 0) {
        /* 持续期间每 500ms 重发一次指令，维持心跳 */
        uint32_t elapsed = 0;
        while (elapsed < duration_s * 1000) {
            vTaskDelay(pdMS_TO_TICKS(500));
            elapsed += 500;
            send_motor_cmd(left, right);
        }
        send_motor_cmd(0, 0);
        ESP_LOGI(TAG, "=== 测试结束 ===");
    } else {
        ESP_LOGI(TAG, "持续运行中，输入 motor_stop 停止");
    }

    return 0;
}

static void register_ripple_run(void)
{
    run_args.left       = arg_int1(NULL, NULL, "<left%>", "左电机速度 (-100 ~ 100)");
    run_args.right      = arg_int1(NULL, NULL, "<right%>", "右电机速度 (-100 ~ 100)");
    run_args.duration_s = arg_int0("t", "time", "<s>", "持续时间(秒)，不指定则持续运行");
    run_args.end        = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "ripple_run",
        .help   = "双电机持续运转 — 配合示波器测量稳态电源纹波",
        .hint   = NULL,
        .func   = &cmd_ripple_run,
        .argtable = &run_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ================================================================== */
/*  Subcommand: ripple_cycle — 启停循环（地弹瞬态测试）                   */
/* ================================================================== */
static struct {
    struct arg_int *interval_s;
    struct arg_int *cycles;
    struct arg_end *end;
} cycle_args;

/* 循环速度序列：0 → +100 → 0 → -100 → 0（论文 5.1.3 测试条件） */
static const int8_t cycle_speeds[] = {0, 100, 0, -100, 0};
#define CYCLE_STEPS (sizeof(cycle_speeds) / sizeof(cycle_speeds[0]))

static int cmd_ripple_cycle(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&cycle_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, cycle_args.end, argv[0]);
        return 1;
    }

    uint32_t interval_s = 5;
    if (cycle_args.interval_s->count > 0) {
        interval_s = (uint32_t)cycle_args.interval_s->ival[0];
        if (interval_s < 2) interval_s = 2;
    }

    uint32_t cycles = 3;
    if (cycle_args.cycles->count > 0) {
        cycles = (uint32_t)cycle_args.cycles->ival[0];
        if (cycles < 1) cycles = 1;
    }

    ESP_LOGI(TAG, "=== 地弹测试: CYCLE ===");
    ESP_LOGI(TAG, "序列: 0 → +100 → 0 → -100 → 0");
    ESP_LOGI(TAG, "每步 %lus, %lu 循环", interval_s, cycles);
    ESP_LOGI(TAG, "示波器: DC 耦合, 200mV/div, 时基 10ms/div, 单次触发");

    for (uint32_t c = 0; c < cycles; c++) {
        ESP_LOGI(TAG, "--- 循环 %lu/%lu ---", c + 1, cycles);

        for (int step = 0; step < CYCLE_STEPS; step++) {
            int8_t spd = cycle_speeds[step];
            ESP_LOGI(TAG, "  [%d] 速度=%d", step, spd);

            send_motor_cmd(spd, spd);

            /* 持续期间每 500ms 重发以维持心跳 */
            uint32_t waited = 0;
            while (waited < interval_s * 1000) {
                vTaskDelay(pdMS_TO_TICKS(500));
                waited += 500;
                send_motor_cmd(spd, spd);
            }
        }
    }

    send_motor_cmd(0, 0);
    ESP_LOGI(TAG, "=== 地弹测试结束 ===");

    return 0;
}

static void register_ripple_cycle(void)
{
    cycle_args.interval_s = arg_int0("i", "interval", "<s>",
                             "每步持续秒数 (默认 5, 最短 2)");
    cycle_args.cycles     = arg_int0("c", "cycles", "<n>",
                             "循环次数 (默认 3)");
    cycle_args.end        = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "ripple_cycle",
        .help   = "电机启停循环 — 配合示波器单次触发捕捉地弹瞬态噪声",
        .hint   = NULL,
        .func   = &cmd_ripple_cycle,
        .argtable = &cycle_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ================================================================== */
/*  Subcommand: ripple_pulse — 单次脉冲                                  */
/* ================================================================== */
static struct {
    struct arg_int *speed;
    struct arg_int *hold_ms;
    struct arg_end *end;
} pulse_args;

static int cmd_ripple_pulse(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&pulse_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pulse_args.end, argv[0]);
        return 1;
    }

    int8_t speed = (int8_t)pulse_args.speed->ival[0];
    if (speed < -100 || speed > 100) {
        ESP_LOGE(TAG, "速度范围: -100 ~ 100");
        return 1;
    }

    uint32_t hold_ms = 2000;
    if (pulse_args.hold_ms->count > 0) {
        hold_ms = (uint32_t)pulse_args.hold_ms->ival[0];
    }

    ESP_LOGI(TAG, "=== 脉冲测试 ===");
    ESP_LOGI(TAG, "0 → %d (保持 %lums) → 0", speed, hold_ms);
    ESP_LOGI(TAG, "示波器: 单次触发, 边沿触发");

    send_motor_cmd(speed, speed);

    uint32_t waited = 0;
    while (waited < hold_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
        send_motor_cmd(speed, speed);
    }

    send_motor_cmd(0, 0);
    ESP_LOGI(TAG, "=== 脉冲结束 ===");
    return 0;
}

static void register_ripple_pulse(void)
{
    pulse_args.speed   = arg_int1(NULL, NULL, "<speed>", "目标速度 (-100 ~ 100)");
    pulse_args.hold_ms = arg_int0("t", "hold", "<ms>", "保持时间 (ms, 默认 2000)");
    pulse_args.end     = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "ripple_pulse",
        .help   = "单次电机脉冲 — 0→目标速度→0，捕捉启停瞬态",
        .hint   = NULL,
        .func   = &cmd_ripple_pulse,
        .argtable = &pulse_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ================================================================== */
/*  Registration                                                        */
/* ================================================================== */

void register_motor_ripple_test(void)
{
    register_ripple_run();
    register_ripple_cycle();
    register_ripple_pulse();
    ESP_LOGI(TAG, "纹波测试命令已注册 (ripple_run, ripple_cycle, ripple_pulse)");
}
