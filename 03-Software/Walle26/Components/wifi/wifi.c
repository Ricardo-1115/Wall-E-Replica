#include "wifi.h"
#include <stdio.h>
#include "esp_log.h"
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
#include "esp_camera.h"


#define WIFI_SSID "8557-2.4G"
#define WIFI_PASS "88888888"
#define IP_GOT_BIT BIT0
static const char *TAG = "WIFI";
static EventGroupHandle_t wifi_event_group;



// 视频流请求处理函数
static esp_err_t stream_handle(httpd_req_t *req){
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char *part_buf[64];

    // 1. 发送 HTTP 头， 告诉浏览器这是一个“混合替换”视频流，并设定边界线
    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=123456789000000000000987654321");
    if(res != ESP_OK) return res;

    // 2. 进入死循环，不断抓拍并发送
    while(true){
        fb = esp_camera_fb_get();       // 从 DMA 抓取一帧图像
        if(!fb){
            ESP_LOGE("CAM", "抓图失败");
            res = ESP_FAIL;
        }
        else {
            // 3. 发送单帧的数据头(包括大小和类型)
            size_t hlen = snprintf((char *)part_buf, 64, "Content-Type: image/jpeg\r\nContent-Length:%u\r\n\r\n", fb->len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
             
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

                        // 发送到舵机控制队列 (假设伺服队列大小大于1，使用xQueueSend)
                        if(servo_cmd_queue != NULL) {
                            xQueueSend(servo_cmd_queue, &s_cmd, 0); 
                        }
                    } else {
                        ESP_LOGW("WS", "舵机数组长度异常 (预期 7，实际 %d)", cJSON_GetArraySize(s_array));
                    }
                }

                cJSON_Delete(root);  // 释放JSON对象内存，防止内存泄漏
            }
            // ------------------------
        }
        free(buf);  // 释放缓冲区内存
    }
    return ESP_OK;  // 返回成功
}

// --- 启动服务器的函数 ---
// 此函数启动HTTP服务器，并注册WebSocket端点，用于建立与客户端的通信通道
// 通信流程中的服务器启动部分：
// - 在WiFi连接成功后调用此函数
// - 创建HTTP服务器实例
// - 注册/ws端点，绑定ws_handler处理函数
// - 客户端可以通过 ws://ESP32_IP/ws 连接WebSocket
void start_web_server()
{
    httpd_handle_t server = NULL;  // 服务器句柄，用于管理HTTP服务器实例
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();  // 使用默认HTTP服务器配置
    config.max_open_sockets = 5;

    // 启动HTTP服务器，如果成功则注册URI处理器
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // 定义WebSocket URI结构体：路径为/ws，方法为GET，处理函数为ws_handler
        // is_websocket=true 表示这是一个WebSocket端点，而非普通HTTP
        static const httpd_uri_t ws_uri = {
            .uri = "/ws",              // WebSocket端点路径
            .method = HTTP_GET,        // HTTP方法（WebSocket握手使用GET）
            .handler = ws_handler,     // 绑定处理函数
            .user_ctx = NULL,          // 用户上下文（此处不需要）
            .is_websocket = true       // 标记为WebSocket端点
        };
        // 注册URI处理器，将/ws路径绑定到ws_handler函数
        httpd_register_uri_handler(server, &ws_uri);
        ESP_LOGI("WS", "WebSocket 服务器启动成功，节点: /ws");  // 记录启动成功日志

        // 注册视频流路由
        httpd_uri_t stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = stream_handle,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
        ESP_LOGI("WIFI", "Web Server 启动成功！图传和指令链路以就绪.");
    }
    // 如果启动失败，函数静默返回（实际应用中应添加错误处理）
}

void App_task(void *pvParameters)
{
    while (1)
    {
        if (IP_GOT_BIT != (xEventGroupGetBits(wifi_event_group) & IP_GOT_BIT))
        {
            ESP_LOGI(TAG, "等待 WiFi 连接...");
        }
        xEventGroupWaitBits(wifi_event_group, IP_GOT_BIT, pdFALSE, pdTRUE, portMAX_DELAY); // 等待 WiFi 连接成功
        start_web_server();                                                                // 连接成功后启动 WebSocket 服务器
        vTaskDelete(NULL);                                                                // 删除当前任务
    }
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi started");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to WiFi");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected from WiFi");
            esp_wifi_connect(); // 重新连接 WiFi
            ESP_LOGI(TAG, "Attempting to reconnect to WiFi...");
            break;
        default:
            break;
        }
    }
    if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_event_group, IP_GOT_BIT); // 设置连接成功的事件位
            break;
        default:
            break;
        }
    }
}

esp_err_t wifi_init(void)
{
    // 外部进行nvs_flash_init()，这里不重复初始化了

    ESP_LOGW(TAG, "1. 初始化阶段");
    ESP_ERROR_CHECK(esp_netif_init());                                                    // 创建一个 LwIP 核心任务，并初始化 LwIP 相关工作
    ESP_ERROR_CHECK(esp_event_loop_create_default());                                     // 创建一个默认事件循环
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);  // 注册一个事件处理程序来处理 WiFi 事件
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL); // 注册一个事件处理程序来处理 IP 事件
    esp_netif_create_default_wifi_sta();                                                  // 创建一个默认的 WIFI station 网络接口实例
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                        // 初始化 WIFI 驱动程序
    wifi_event_group = xEventGroupCreate();                      // 创建一个事件组来管理 WiFi 连接状态
    xTaskCreate(App_task, "wifi_app_task", 4096, NULL, 5, NULL); // 创建一个新的 FreeRTOS 任务来运行应用程序逻辑

    ESP_LOGW(TAG, "2. 配置阶段");
    esp_wifi_set_mode(WIFI_MODE_STA); // 设置 WiFi 模式为 Station 模式
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS},
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config)); // 设置 WIFI 配置

    ESP_LOGW(TAG, "3. 启动阶段");
    ESP_ERROR_CHECK(esp_wifi_start()); // 启动 WIFI 驱动程序
    // esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGW(TAG, "4. 连接阶段");
    ESP_ERROR_CHECK(esp_wifi_connect()); // 连接到配置的 WiFi 网络

    return ESP_OK;
}


