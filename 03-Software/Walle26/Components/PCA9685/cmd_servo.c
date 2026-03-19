#include "PCA9685.h"
#include <stdio.h>
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
    servo_args.angle = arg_dbl1(NULL, NULL, "angle", "Servo angle (0-180)");
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

// Argument structure for servo_key command
static struct {
    struct arg_int *channel;
    struct arg_end *end;
} servo_key_args;

// Command function for interactive servo control with keyboard input
static int cmd_servo_key(int argc, char **argv){
    // Parse command line arguments
    int nerrors = arg_parse(argc, argv, (void **)&servo_key_args);
    if(nerrors != 0){
        arg_print_errors(stderr, servo_key_args.end, argv[0]);
        return -1;
    }

    // Validate channel number
    int channel = servo_key_args.channel->ival[0];
    if(channel < 0 || channel > 15){
        ESP_LOGE("cmd_servo_key", "Invalid channel: %d. Channel must be between 0 and 15.", channel);
        return -1;
    }

    // Initialize servo to 90 degrees as starting position
    float current_angle = 90.0f;
    pca9685_set_angle(channel, current_angle);
    ESP_LOGI("cmd_servo_key", "Servo %d initialized to %.1f degrees. Use 'W' to increase, 'S' to decrease, 'A' to increase by 10, 'D' to decrease by 10, 'Q' to quit.", channel, current_angle);

    // Main control loop for keyboard input
    char input_buffer[10];  // Buffer for user input
    while(1) {
        // Display current angle and prompt for input
        printf("Current angle: %.1f degrees. Press W/S/Q/A/D: ", current_angle);
        fflush(stdout);  // Ensure prompt is displayed immediately

        // Read a line of input from user
        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
            break;  // Exit if input fails
        }

        // Get the first character from input
        int ch = input_buffer[0];
        if (ch == '\0' || ch == '\n') continue;  // Skip empty input

        // Handle 'W' key: increase angle by 1 degree
        if(ch == 'W' || ch == 'w') {
            if(current_angle < 180.0f) {
                current_angle += 1.0f;
                pca9685_set_angle(channel, current_angle);
                ESP_LOGI("cmd_servo_key", "Angle increased to %.1f degrees", current_angle);
            } else {
                ESP_LOGW("cmd_servo_key", "Angle already at maximum (180 degrees)");
            }
        }
        // Handle 'S' key: decrease angle by 1 degree
        else if(ch == 'S' || ch == 's') {
            if(current_angle > 0.0f) {
                current_angle -= 1.0f;
                pca9685_set_angle(channel, current_angle);
                ESP_LOGI("cmd_servo_key", "Angle decreased to %.1f degrees", current_angle);
            } else {
                ESP_LOGW("cmd_servo_key", "Angle already at minimum (0 degrees)");
            }
        }
        else if(ch == 'A' || ch == 'a') {
            if(current_angle < 180.0f) {
                current_angle += 10.0f;
                if(current_angle > 180.0f) current_angle = 180.0f; // Clamp to max
                pca9685_set_angle(channel, current_angle);
                ESP_LOGI("cmd_servo_key", "Angle increased to %.1f degrees", current_angle);
            } else {
                ESP_LOGW("cmd_servo_key", "Angle already at maximum (180 degrees)");
            }
        }
        else if(ch == 'D' || ch == 'd') {
            if(current_angle > 0.0f) {
                current_angle -= 10.0f;
                if(current_angle < 0.0f) current_angle = 0.0f; // Clamp to min
                pca9685_set_angle(channel, current_angle);
                ESP_LOGI("cmd_servo_key", "Angle decreased to %.1f degrees", current_angle);
            } else {
                ESP_LOGW("cmd_servo_key", "Angle already at minimum (0 degrees)");
            }
        }
        // Handle 'Q' key: quit the control loop
        else if(ch == 'Q' || ch == 'q') {
            current_angle = 90.0f;
            pca9685_set_angle(channel, current_angle); // Reset to neutral position before exiting
            ESP_LOGI("cmd_servo_key", "Exiting servo key control, resetting angle to 90 degrees.");
            break;
        }
        // Handle invalid keys
        else {
            ESP_LOGW("cmd_servo_key", "Invalid key: %c. Use W/S/Q/A/D.", ch);
        }
    }

    return 0;
}

void register_servo_key(){
    // Register the new servo_key command for keyboard control
    servo_key_args.channel = arg_int1(NULL, NULL, "channel", "Servo channel (0-15)");
    servo_key_args.end = arg_end(2);

    const esp_console_cmd_t cmd_servo_key_cmd = {
        .command = "servo_key",
        .help = "Interactive servo control with W/S keys (W: +1 deg, S: -1 deg, A: +10 deg, D: -10 deg, Q: quit)",
        .hint = NULL,
        .func = cmd_servo_key,
        .argtable = &servo_key_args
    };
    esp_console_cmd_register(&cmd_servo_key_cmd);
}

