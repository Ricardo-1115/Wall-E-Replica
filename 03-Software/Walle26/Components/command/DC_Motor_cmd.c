#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DC_Motor.h"

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
    motor_stop_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "motor_stop",
        .help = "Stop the motor with optional smooth fading. Usage: motor_stop [duration_ms] [--info]",
        .hint = NULL,
        .func = &cmd_motor_stop,
        .argtable = &motor_stop_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
