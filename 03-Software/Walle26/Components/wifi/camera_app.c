#include "esp_camera.h"
#include "esp_log.h"

// camera gpio configure
#define CAM_PIN_PWDN       -1      // 电源控制脚
#define CAM_PIN_RESET      -1      // 复位引脚

#define CAM_PIN_XCLK       12
#define CAM_PIN_SIOD       48
#define CAM_PIN_SIOC       47
#define CAM_PIN_Y9         13
#define CAM_PIN_Y8         11
#define CAM_PIN_Y7         10
#define CAM_PIN_Y6         15
#define CAM_PIN_Y5         17
#define CAM_PIN_Y4         8
#define CAM_PIN_Y3         18
#define CAM_PIN_Y2         16
#define CAM_PIN_VSYNC      21
#define CAM_PIN_HREF       14
#define CAM_PIN_PCLK       9

void init_camera(void){
    camera_config_t config = { 0 };
    config.ledc_timer = LEDC_TIMER_1;
    config.ledc_channel = LEDC_CHANNEL_2;   /* 不与 DC 电机使用的 CH0/CH1 冲突 */
    config.pin_d0 = CAM_PIN_Y2;
    config.pin_d1 = CAM_PIN_Y3;
    config.pin_d2 = CAM_PIN_Y4;
    config.pin_d3 = CAM_PIN_Y5;
    config.pin_d4 = CAM_PIN_Y6;
    config.pin_d5 = CAM_PIN_Y7;
    config.pin_d6 = CAM_PIN_Y8;
    config.pin_d7 = CAM_PIN_Y9;
    config.pin_xclk = CAM_PIN_XCLK;
    config.pin_pclk = CAM_PIN_PCLK;
    config.pin_vsync = CAM_PIN_VSYNC;
    config.pin_href = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_reset = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;
    config.frame_size = FRAMESIZE_VGA;       // 分辨率：VGA(640 x 480)
    config.pixel_format = PIXFORMAT_JPEG;    // 硬件直接输出JPEF
    config.jpeg_quality = 12;                // 压缩质量(0 - 63，越小质量越高但体积越大)
    config.fb_count= 2;                      // 开启双缓冲区
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if(err != ESP_OK){
        ESP_LOGE("CAMERA", "摄像头初始化失败：0x%x", err);
        return;
    }
    ESP_LOGI("CAMERA", "摄像头初始化成功！");
}
