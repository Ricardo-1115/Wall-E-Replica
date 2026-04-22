#include "Servo.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  
#include "Servo_app.h"


static const char *TAG = "CMD_SERVO";

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
        ESP_LOGE(TAG, "Invalid channel: %d. Channel must be between 0 and 15.", channel);
        return -1;
    }
    float angle = (float)servo_args.angle->dval[0];
    if(angle < 0.0f || angle > 180.0f){
        ESP_LOGE(TAG, "Invalid angle: %.2f. Angle must be between 0 and 180.", angle);
        return -1;
    }
    uint32_t duration_ms = (uint32_t)servo_args.duration_ms->ival[0];

    walle_move_servo(channel, angle, duration_ms);
    ESP_LOGI(TAG, "Moving servo %d to %.2f degrees over %" PRIu32 " ms", channel, angle, duration_ms);
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
        ESP_LOGE(TAG, "Invalid channel: %d. Channel must be between 0 and 15.", channel);
        return -1;
    }

    // Initialize servo to 90 degrees as starting position
    float current_angle = 90.0f;
    pca9685_set_angle(channel, current_angle);
    ESP_LOGI(TAG, "Servo %d initialized to %.1f degrees. Use 'W' to increase, 'S' to decrease, 'A' to increase by 10, 'D' to decrease by 10, 'Q' to quit.", channel, current_angle);

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
                ESP_LOGI(TAG, "Angle increased to %.1f degrees", current_angle);
            } else {
                ESP_LOGW(TAG, "Angle already at maximum (180 degrees)");
            }
        }
        // Handle 'S' key: decrease angle by 1 degree
        else if(ch == 'S' || ch == 's') {
            if(current_angle > 0.0f) {
                current_angle -= 1.0f;
                pca9685_set_angle(channel, current_angle);
                ESP_LOGI(TAG, "Angle decreased to %.1f degrees", current_angle);
            } else {
                ESP_LOGW(TAG, "Angle already at minimum (0 degrees)");
            }
        }
        else if(ch == 'A' || ch == 'a') {
            if(current_angle < 180.0f) {
                current_angle += 10.0f;
                if(current_angle > 180.0f) current_angle = 180.0f; // Clamp to max
                pca9685_set_angle(channel, current_angle);
                ESP_LOGI(TAG, "Angle increased to %.1f degrees", current_angle);
            } else {
                ESP_LOGW(TAG, "Angle already at maximum (180 degrees)");
            }
        }
        else if(ch == 'D' || ch == 'd') {
            if(current_angle > 0.0f) {
                current_angle -= 10.0f;
                if(current_angle < 0.0f) current_angle = 0.0f; // Clamp to min
                pca9685_set_angle(channel, current_angle);
                ESP_LOGI(TAG, "Angle decreased to %.1f degrees", current_angle);
            } else {
                ESP_LOGW(TAG, "Angle already at minimum (0 degrees)");
            }
        }
        // Handle 'Q' key: quit the control loop
        else if(ch == 'Q' || ch == 'q') {
            current_angle = 90.0f;
            pca9685_set_angle(channel, current_angle); // Reset to neutral position before exiting
            ESP_LOGI(TAG, "Exiting servo key control, resetting angle to 90 degrees.");
            break;
        }
        // Handle invalid keys
        else {
            ESP_LOGW(TAG, "Invalid key: %c. Use W/S/Q/A/D.", ch);
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

static void save_joint_config_to_nvs(void){
    nvs_handle_t my_handle;
    // Open
    ESP_ERROR_CHECK(nvs_open(TAG, NVS_READWRITE, &my_handle));

    // Write
    esp_err_t err = nvs_set_blob(my_handle, "walle_joint", walle_joint, sizeof(walle_joint));
    if(err != ESP_OK){
        ESP_LOGE(TAG, "保存 Blob 失败! 错误码: %s", esp_err_to_name(err));
    }
    else{
        err = nvs_commit(my_handle);
        if(err == ESP_OK){
            ESP_LOGI(TAG, "关节参数已成功写入NVS.");
        }
    }

    // Close
    nvs_close(my_handle);
}

void load_joint_config_from_nvs(void){
    nvs_handle_t my_handle;
    esp_err_t err;

    // 1. 打开 NVS, 模式为只读
    err = nvs_open(TAG, NVS_READONLY, &my_handle);
    if(err != ESP_OK){
        ESP_LOGW(TAG, "NVS尚未初始化或没有数据，将使用默认数据！");
        return;
    }

    size_t require_size = 0;
    // 2. 第一次调用nvs_get_blob：传入 NULL 获取保存在 NVS 的数据长度
    err = nvs_get_blob(my_handle, "walle_joint", NULL, &require_size);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "读取 nvs 数据长度失败");
        nvs_close(my_handle);
        return;
    }

    // 3. 检查有没有找到数据， 并且数据长度是否与我们现在结构体数组大小一致
    if(require_size == sizeof(walle_joint)){
        // 4. 第二次调用 nvs_get_blob, 真正把数据读取到 walle_joint 数组中
        err = nvs_get_blob(my_handle, "walle_joint", walle_joint, &require_size);
        if(err == ESP_OK){
            ESP_LOGI(TAG, "成功从 NVS 加载了7个关节的配置参数");
        }
    }
    else if(require_size > 0){
        ESP_LOGE(TAG, "NVS 中的数据大小(%d)与当前结构体大小(%d)不匹配！放弃读取，使用默认值", require_size, sizeof(walle_joint));
    }
    else{
        ESP_LOGI(TAG, "NVS 中没有找到关节配置数据，使用代码中的默认初始化值。");
    }

    // 5. 关闭句柄
    nvs_close(my_handle);
}

static struct {
    struct arg_int *channel;
    struct arg_dbl *min_angle;
    struct arg_dbl *max_angle;
    struct arg_int *reverse;
    struct arg_lit *save_cfg;
    struct arg_end *end;
} servo_calib;

static int cmd_servo_calib(int argc, char **argv){
    int nerrors = arg_parse(argc, argv, (void **) &servo_calib);
    if(nerrors != 0){
        arg_print_errors(stderr, servo_calib.end, argv[0]);
    }
    
    int channel = servo_calib.channel->ival[0];
    if(channel < 0 || channel > 15){
        ESP_LOGE(TAG, "无效的通道号: %d. 机器人关节编号必须是 0 到 6.", channel);
        return -1;
    }
    float min_angle = (float)servo_calib.min_angle->dval[0];
    float max_angle = (float)servo_calib.max_angle->dval[0];
    int reverse = servo_calib.reverse->ival[0];
    
    if(min_angle < 0.0f || min_angle > 180.0f || max_angle < 0.0f || max_angle > 180.0f){
        ESP_LOGE(TAG, "角度无效，必须在 0 到 180 度之间.");
        return 1;
    }

    // 将调试命令值写入关节参数结构体
    walle_joint[channel].min_angle = min_angle;
    walle_joint[channel].max_angle = max_angle;
    walle_joint[channel].reverse = (bool)reverse;

    if(servo_calib.save_cfg->count > 0){
        save_joint_config_to_nvs();
        ESP_LOGI(TAG, "参数已保存至 NVS. 当前关节配置表：");
        ESP_LOGI(TAG, "----------------------------------------");
        ESP_LOGI(TAG, " ID |  Min  |  Max  | Reverse ");
        for(int i = 0; i < 7; i++){
            ESP_LOGI(TAG, " %2d | %5.1f | %5.1f |   %d", 
                     i, walle_joint[i].min_angle, walle_joint[i].max_angle, walle_joint[i].reverse);
        }
        ESP_LOGI(TAG, "----------------------------------------");
    }
    return 0;
}

void register_servo_calib(void){
    servo_calib.channel = arg_int1(NULL, NULL, "channel", "Joint ID (0-6)");
    servo_calib.min_angle = arg_dbl1(NULL, NULL, "min_angle", "Minimum safe angle (0-180)");
    servo_calib.max_angle = arg_dbl1(NULL, NULL, "max_angle", "Maximum safe angle (0-180)");
    servo_calib.reverse = arg_int1(NULL, NULL, "reverse", "0: Normal, 1: Reversed");
    servo_calib.save_cfg = arg_lit0("s", NULL, "Save configuration to NVS flash");
    servo_calib.end = arg_end(5);

    const esp_console_cmd_t servo_calib_cmd = {
        .command = "servo_calib",
        .help = "Calibrate servo joint limits and reverse flag, optionally save to NVS", // 更新了正规的帮助信息
        .hint = NULL,
        .func = cmd_servo_calib,
        .argtable = &servo_calib
    };
    esp_console_cmd_register(&servo_calib_cmd);
}
