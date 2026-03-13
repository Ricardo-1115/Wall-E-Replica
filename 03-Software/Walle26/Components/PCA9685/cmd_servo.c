#include "PCA9685.h"
#include <stdint.h>
#include <inttypes.h>
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static struct {
    struct arg_int *channel;
    struct arg_dbl *angle;
    struct arg_int *duration_ms;
    struct arg_end *end;
} servo_args;

static int cmd_servo_move(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **)&servo_args);
    if(nerrors != 0){
        arg_print_errors(stderr, servo_args.end, argv[0]);
    }

    int channel = servo_args.channel->ival[0];
    if(channel < 0 || channel > 15){
        ESP_LOGE("cmd_servo", "Invalid channel: %d. Channel must be between 0 and 15.", channel);
        return -1;
    }
    float angle = (float)servo_args.angle->dval[0];
    if(angle < 0.0f || angle > 180.0f){
        ESP_LOGE("cmd_servo", "Invalid angle: %.2f. Angle must be between 0 and 180.", angle);
        return -1;
    }
    uint32_t duration_ms = (uint32_t)servo_args.duration_ms->ival[0];

    walle_move_servo(channel, angle, duration_ms);
    ESP_LOGI("cmd_servo", "Moving servo %d to %.2f degrees over %" PRIu32 " ms", channel, angle, duration_ms);
    return 0;
}


void register_servo(){
    servo_args.channel = arg_int1(NULL, NULL, "channel", "Servo channel (0-15)");
    servo_args.angle = arg_dbl1(NULL, NULL, "<0.0-180.0>", "Servo angle (0-180)");
    servo_args.duration_ms = arg_int1(NULL, NULL, "duration", "Movement duration (ms)");
    servo_args.end = arg_end(4);

    const esp_console_cmd_t cmd_servo = {
        .command = "servo",
        .help = "Move servo to specified angle over specified duration",
        .hint = NULL,
        .func = &cmd_servo_move,
        .argtable = &servo_args
    };
    esp_console_cmd_register(&cmd_servo);

}



