#include "wifi.h"
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_http_server.h"
#include "cJSON.h"        // 修复 cJSON_Parse 找不到的报错
#include <sys/param.h>    // 修复 MAX 和 MIN 找不到的报错
#include "DC_Motor.h"     // 引入电机队列 motor_cmd_queue 和 motor_cmd_t


#define WIFI_SSID "Redmi K50"
#define WIFI_PASS "th5360778131@"
#define IP_GOT_BIT BIT0
static const char *TAG = "WIFI";
static EventGroupHandle_t wifi_event_group;


// --- WebSocket 核心处理函数 ---
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI("WS", "WebSocket 握手成功");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len > 0) {
        buf = calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK) {
            
            // --- 核心解析与入队逻辑 ---
            cJSON *root = cJSON_Parse((char *)ws_pkt.payload);
            if (root != NULL) {
                cJSON *l_item = cJSON_GetObjectItem(root, "L");
                cJSON *r_item = cJSON_GetObjectItem(root, "R");

                if (cJSON_IsNumber(l_item) && cJSON_IsNumber(r_item)) {
                    motor_cmd_t new_cmd;
                    // 钳位保护，防止越界
                    new_cmd.left_speed = (int8_t)MAX(-100, MIN(100, l_item->valueint));
                    new_cmd.right_speed = (int8_t)MAX(-100, MIN(100, r_item->valueint));

                    // 将最新指令覆盖写入长度为 1 的队列
                    // 即使队列满了也会覆盖旧数据，保证底层拿到的是最新指令
                    xQueueOverwrite(motor_cmd_queue, &new_cmd);
                }
                cJSON_Delete(root); // 极其重要：防止内存泄漏！
            }
            // ------------------------

        }
        free(buf);
    }
    return ESP_OK;
}

// --- 启动服务器的函数 ---
void start_web_server()
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // 启动 HTTP Server
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // 注册 WebSocket 节点： ws://IP地址/ws
        static const httpd_uri_t ws_uri = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .user_ctx = NULL,
            .is_websocket = true // 必须标记为 true
        };
        httpd_register_uri_handler(server, &ws_uri);
        ESP_LOGI("WS", "WebSocket 服务器启动成功，节点: /ws");
    }
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


