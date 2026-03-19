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


#define WIFI_SSID "8557-2.4G"
#define WIFI_PASS "88888888"
#define IP_GOT_BIT BIT0
static const char *TAG = "WIFI";
static EventGroupHandle_t wifi_event_group;


// --- WebSocket 核心处理函数 ---
// 此函数处理 WebSocket 连接和消息接收，用于接收客户端发送的电机控制命令
// 通信流程：
// 1. 客户端（例如手机App）通过WebSocket连接到ESP32的/ws端点
// 2. 客户端发送JSON格式的消息，如 {"L": 50, "R": -30} 表示左电机速度50，右电机速度-30
// 3. ESP32接收消息，解析JSON，提取速度值，放入电机命令队列
// 4. 电机控制任务从队列读取命令，异步控制电机速度
// 5. 客户端可以持续发送新命令，实现实时控制
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
            // 解析接收到的JSON数据
            cJSON *root = cJSON_Parse((char *)ws_pkt.payload);
            if (root != NULL) {
                // 提取JSON中的"L"（左电机速度）和"R"（右电机速度）字段
                cJSON *l_item = cJSON_GetObjectItem(root, "L");
                cJSON *r_item = cJSON_GetObjectItem(root, "R");

                // 检查字段是否存在且为数字
                if (cJSON_IsNumber(l_item) && cJSON_IsNumber(r_item)) {
                    motor_cmd_t new_cmd;  // 创建新的电机命令结构体
                    // 钳位保护：确保速度值在-100到100范围内，防止越界
                    new_cmd.left_speed = (int8_t)MAX(-100, MIN(100, l_item->valueint));
                    new_cmd.right_speed = (int8_t)MAX(-100, MIN(100, r_item->valueint));

                    // 将最新指令覆盖写入长度为1的队列
                    // 即使队列满了也会覆盖旧数据，保证底层拿到的是最新指令（实时控制的关键）
                    xQueueOverwrite(motor_cmd_queue, &new_cmd);
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

    ESP_LOGW(TAG, "4. 连接阶段");
    ESP_ERROR_CHECK(esp_wifi_connect()); // 连接到配置的 WiFi 网络

    return ESP_OK;
}


