#include "wifi.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <sys/param.h>
#include "DC_Motor.h"
#include "Servo_app.h"
#include "energy_manager.h"
#include "animation_engine.h"
#include "battery.h"
#include "esp_netif.h"
#include "esp_camera.h"
#include "esp_mac.h"


#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASS CONFIG_WIFI_PASSWORD
#define IP_GOT_BIT BIT0
static const char *TAG = "WIFI";
static EventGroupHandle_t wifi_event_group;



// 视频流请求处理函数
static esp_err_t stream_handle(httpd_req_t *req){
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    static uint32_t s_fc = 0;
    static int64_t s_t0 = 0;

    // 1. 发送 HTTP 头， 告诉浏览器这是一个"混合替换"视频流，并设定边界线
    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=123456789000000000000987654321");
    if(res != ESP_OK) return res;

    // 2. 进入死循环，不断抓拍并发送
    while(true){
        fb = esp_camera_fb_get();       // 从 DMA 抓取一帧图像
        if(!fb){
            ESP_LOGE("CAM", "cam frame err");
            res = ESP_FAIL;
        }
        else {
            s_fc++;
            // if (s_fc == 1) s_t0 = esp_timer_get_time();
            // if (s_fc % 100 == 0) {
            //     float fps = (float)s_fc * 1000000.0f / (float)(esp_timer_get_time() - s_t0);
            //     ESP_LOGI("STREAM", "stream fps: %.1f FPS (%u fr)", fps, s_fc);
            // }

            // 3. 发送单帧的数据头(包括大小和类型)
            size_t hlen = snprintf(part_buf, 64, "Content-Type: image/jpeg\r\nContent-Length:%u\r\n\r\n", fb->len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
             
            // 4. 发送真正的 JPEG 图像数据
            if(res == ESP_OK){
                res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
            }

            // 5. 发送边界线，标志着这一帧结束
            if(res == ESP_OK){
                res = httpd_resp_send_chunk(req, "\r\n--123456789000000000000987654321\r\n", 37);
            }
            esp_camera_fb_return(fb);       // 释放这一帧内存，还给底层驱动
        }

         // 如果客户端断开了连接， res会变成 ESP_FAIL， 此时必须 break 退出死循环
         if(res != ESP_OK) break;
    }
    return res;
    
}

// --- WebSocket 核心处理函数 ---
// 此函数处理 WebSocket 连接和消息接收，用于接收客户端发送的电机和舵机控制命令
// 通信流程：
// 1. 客户端（例如手机App/浏览器）通过WebSocket连接到ESP32的/ws端点
// 2. 客户端发送JSON格式的消息，例如：
//    {"L": 50, "R": -30, "S": [50.0, 50.0, 50.0, 50.0, 50.0, 0.0, 0.0], "D": 100}
//    - L/R: 左右履带电机速度（-100 到 100）
//    - S: 7个伺服关节的目标角度百分比（0.0 到 100.0）
//    - D: 伺服关节动作的缓动持续时间（ms）
// 3. ESP32接收消息并解析JSON，提取动力和关节数据
// 4. 将提取的数据分别放入对应的 FreeRTOS 队列 (motor_cmd_queue, servo_cmd_queue)
// 5. 底层电机和舵机控制任务从队列中读取命令，进行异步平滑控制
static esp_err_t ws_handler(httpd_req_t *req) {
    // 处理WebSocket握手请求（HTTP GET）
    if (req->method == HTTP_GET) {
        ESP_LOGI("WS", "WebSocket 握手成功");  // 记录握手成功日志
        return ESP_OK;  // 返回成功，完成握手
    }

    // 初始化WebSocket数据包结构体，用于接收数据
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;  // 缓冲区指针，用于存储接收到的数据
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));  // 清空结构体
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;  // 设置为文本类型（JSON消息）

    // 第一次调用httpd_ws_recv_frame，获取数据包长度（payload为NULL，只获取元数据）
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;  // 如果出错，直接返回

    // 如果接收到数据（长度大于0）
    if (ws_pkt.len > 0) {
        // 分配缓冲区存储数据（多分配1字节用于字符串结束符）
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;  // 内存分配失败
        ws_pkt.payload = buf;  // 设置payload指向缓冲区

        // 第二次调用httpd_ws_recv_frame，实际接收数据到缓冲区
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK) {
            // --- 收到用户操控指令，立即停止动画干扰 ---
            anim_engine_stop();
            anim_engine_enable_idle(false);
            energy_manager_notify_activity();

            // --- 核心解析与入队逻辑 ---
            cJSON *root = cJSON_Parse((char *)ws_pkt.payload);
            if (root != NULL) {
                
                // ==========================================
                // [模块 A] 履带直流电机数据解析 (L, R)
                // ==========================================
                cJSON *l_item = cJSON_GetObjectItem(root, "L");
                cJSON *r_item = cJSON_GetObjectItem(root, "R");

                if (cJSON_IsNumber(l_item) && cJSON_IsNumber(r_item)) {
                    motor_cmd_t new_cmd;  
                    // 钳位保护：确保速度值在-100到100范围内，防止越界
                    new_cmd.left_speed = (int8_t)MAX(-100, MIN(100, l_item->valueint));
                    new_cmd.right_speed = (int8_t)MAX(-100, MIN(100, r_item->valueint));

                    // 将最新指令覆盖写入长度为1的队列 (适用于 motor_cmd_queue)
                    if(motor_cmd_queue != NULL) {
                        xQueueOverwrite(motor_cmd_queue, &new_cmd);
                    }
                }

                // ==========================================
                // [模块 B] 伺服电机动作数据解析 (S 数组, D 延时)
                // ==========================================
                cJSON *s_array = cJSON_GetObjectItem(root, "S");
                if (s_array != NULL && cJSON_IsArray(s_array)) {
                    if (cJSON_GetArraySize(s_array) == 7) {
                        servo_cmd_t s_cmd;

                        // 提取 D (Duration) 参数，没有则默认 100ms
                        cJSON *d_item = cJSON_GetObjectItem(root, "D");
                        if (d_item != NULL && cJSON_IsNumber(d_item)) {
                            s_cmd.duration_ms = (uint32_t)MAX(0, d_item->valueint);
                        } else {
                            s_cmd.duration_ms = 100;
                        }

                        // 提取 7 个关节的百分比，增加安全钳位保护 (0.0 到 100.0)
                        for (int i = 0; i < 7; i++) {
                            cJSON *item = cJSON_GetArrayItem(s_array, i);
                            if (item != NULL && cJSON_IsNumber(item)) {
                                s_cmd.percentages[i] = (float)MAX(0.0, MIN(100.0, item->valuedouble));
                            } else {
                                s_cmd.percentages[i] = 50.0f; // 异常数据强制回中
                            }
                        }

                        // 覆盖写入深度为1的队列（与电机队列策略一致，保证始终执行最新指令）
                        if(servo_cmd_queue != NULL) {
                            xQueueOverwrite(servo_cmd_queue, &s_cmd);
                        }
                    } else {
                        ESP_LOGW("WS", "舵机数组长度异常 (预期 7，实际 %d)", cJSON_GetArraySize(s_array));
                    }
                }

                cJSON_Delete(root);  // 释放JSON对象内存，防止内存泄漏
            }
            // ------------------------

            /* --- WebSocket 回声：原路返回（供延迟测试 RTT/2） --- */
            httpd_ws_frame_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.type = HTTPD_WS_TYPE_TEXT;
            resp.payload = ws_pkt.payload;
            resp.len = ws_pkt.len;
            httpd_ws_send_frame(req, &resp);
        }
        free(buf);  // 释放缓冲区内存
    }
    return ESP_OK;  // 返回成功
}

/* --- /status JSON 端点：返回电量、energy、状态（供前端轮询） --- */
static esp_err_t status_handler(httpd_req_t *req) {
    uint8_t batt = battery_get_percentage();
    uint8_t energy = energy_manager_get_value();
    int zone = energy_manager_get_zone();
    energy_state_t state = energy_manager_get_state();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "battery", batt);
    cJSON_AddNumberToObject(root, "energy", energy);
    cJSON_AddNumberToObject(root, "zone", zone);
    cJSON_AddBoolToObject(root, "engaged", (state == ENERGY_STATE_ENGAGED));

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* --- 主页：返回控制面板 HTML（由 cmake EMBED_FILES 嵌入固件） --- */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

// --- 启动服务器的函数 ---
// 此函数启动HTTP服务器，并注册WebSocket端点，用于建立与客户端的通信通道
// 通信流程中的服务器启动部分：
// - 在WiFi连接成功后调用此函数
// - 创建HTTP服务器实例
// - 注册/ws端点，绑定ws_handler处理函数
// - 客户端可以通过 ws://ESP32_IP/ws 连接WebSocket
// - 图传流媒体监听 :8081，与控制服务器分离，避免 while(true) 阻塞 WebSocket
void start_web_server()
{
    httpd_handle_t server_ctrl = NULL;
    httpd_handle_t server_stream = NULL;

    // ===== 控制服务器 (端口 80) =====
    httpd_config_t config_ctrl = HTTPD_DEFAULT_CONFIG();
    config_ctrl.core_id = 0;             // 绑定 Core 0（网络/协议核）
    config_ctrl.server_port = 80;
    config_ctrl.max_open_sockets = 5;

    if (httpd_start(&server_ctrl, &config_ctrl) == ESP_OK)
    {
        /* --- 主页 / --- */
        static const httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server_ctrl, &root_uri);

        static const httpd_uri_t ws_uri = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .user_ctx = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server_ctrl, &ws_uri);

        /* --- 注册 /status 状态查询端点 --- */
        static const httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server_ctrl, &status_uri);

        ESP_LOGI("WS", "WebSocket 控制服务器启动成功");
    }
    else {
        ESP_LOGE("WS", "控制服务器启动失败！");
    }

    // ===== 流媒体服务器 (端口 8081，独立于控制通道) =====
    httpd_config_t config_stream = HTTPD_DEFAULT_CONFIG();
    config_stream.core_id = 0;
    config_stream.server_port = 8081;
    config_stream.ctrl_port = 32769;         // 与控制服务器(32768)错开，避免 EADDRINUSE
    config_stream.max_open_sockets = 3;              // 只服务图传，无需太多连接
    config_stream.task_priority = tskIDLE_PRIORITY + 2; // 低于控制服务器，不抢 CPU
    config_stream.stack_size = 3072;                 // 图传不需要大栈

    if (httpd_start(&server_stream, &config_stream) == ESP_OK)
    {
        httpd_uri_t stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = stream_handle,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server_stream, &stream_uri);
        ESP_LOGI("WS", "图传流媒体服务器启动成功 -> http://IP:8081/stream");
        ESP_LOGI("WIFI", "Web Server 全部启动！控制:80 | 图传:8081");
    }
    else {
        ESP_LOGE("WS", "图传服务器启动失败！");
    }
}

/* --- STA 事件处理 --- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi 已启动，开始连接...");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "已连接到 AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi 断开，5 秒后重连...");
            esp_wifi_connect();
            break;
        default:
            break;
        }
    }
    if (event_base == IP_EVENT) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "获取到 IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_event_group, IP_GOT_BIT);
            break;
        }
        default:
            break;
        }
    }
}

/* 等待 IP 后启动 Web 服务器的任务 */
static void App_task(void *pvParameters)
{
    xEventGroupWaitBits(wifi_event_group, IP_GOT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi 连接成功，启动 Web 服务器...");
    start_web_server();
    vTaskDelete(NULL);
}

esp_err_t wifi_init(void)
{
    ESP_LOGW(TAG, "1. 初始化阶段");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* DHCP 自动获取 IP */

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_event_group = xEventGroupCreate();

    ESP_LOGW(TAG, "2. 配置阶段");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGW(TAG, "3. 启动阶段");
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* 创建等待 IP 的后台任务 */
    xTaskCreatePinnedToCore(App_task, "wifi_app_task", 4096, NULL, 5, NULL, 0);

    ESP_LOGW(TAG, "4. 连接阶段");
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "正在连接到 WiFi '%s'...", WIFI_SSID);

    return ESP_OK;
}


