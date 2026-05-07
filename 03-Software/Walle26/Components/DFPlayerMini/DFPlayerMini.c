#include "DFPlayerMini.h"
#include "esp_timer.h"

const int uart_buffer_size = (512);
const static char *TAG = "DFPlayerMini";

esp_err_t DFPlayerMini_Init(void)
{
    const uart_port_t uart_num = UART_NUM_2;
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .parity = UART_PARITY_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
        .stop_bits = UART_STOP_BITS_1};

    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size, uart_buffer_size, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, 6, 7, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // uint8_t rx_buffer[19];
    // uart_read_bytes(uart_num, &rx_buffer, sizeof(rx_buffer), 3000 / portTICK_PERIOD_MS);
    // ESP_LOGI(TAG, "DFPlayer 初始化成功!");
    // return ESP_OK;
    uint8_t rx_buffer[15];
    bool detected = false;
    uint64_t start_time = esp_timer_get_time();

    while (esp_timer_get_time() - start_time < 4 * 1000 * 1000)
    {
        int len = uart_read_bytes(uart_num, rx_buffer, 10, pdMS_TO_TICKS(50));
        if (len >= 10 && rx_buffer[0] == 0x7E && rx_buffer[3] == 0x3F && rx_buffer[9] == 0xEF)
        {
            // 校验通过：确实是 DFPlayer 初始化成功帧
            uint8_t device_status = rx_buffer[6];
            ESP_LOGI(TAG, "DFPlayer 初始化成功，在线设备: 0x%02X", device_status);
            detected = true;
            break;
        }
    }
     if (!detected) {
          ESP_LOGW(TAG, "未收到 DFPlayer 就绪信号，可能未\"冷启动\"或接线异常");
      }

      // 清空缓冲区，避免残留数据干扰后续通信
      uart_flush(uart_num);
      return ESP_OK;
}

static void send_cmd(uint8_t cmd, uint16_t param)
{
    uint8_t frame[10];
    uint8_t len = 6; // 固定：版本+长度+命令+反馈+参数1+参数2

    frame[0] = 0x7E;
    frame[1] = 0xFF;
    frame[2] = len;
    frame[3] = cmd;
    frame[4] = 0x00; // no feedback
    frame[5] = (param >> 8) & 0xFF;
    frame[6] = param & 0xFF;

    // 计算校验和
    uint16_t sum = 0;
    for (uint8_t i = 1; i <= 6; ++i)
    {
        sum += frame[i];
    }
    uint16_t checksum = 0xFFFF - sum + 1;
    frame[7] = (checksum >> 8) & 0xFF;
    frame[8] = checksum & 0xFF;
    frame[9] = 0xEF;

    ESP_LOGI(TAG, "发送指令: cmd=0x%02X, frame[5]=0x%02X, frame[6]=0x%02X, frame[7]=0x%02X, frame[8]=0x%02X", cmd, frame[5], frame[6], frame[7], frame[8]);

    uart_write_bytes(UART_NUM_2, frame, sizeof(frame));
}

// 调整音量
void DFPlayerMini_SetVolume(uint8_t volume)
{
    volume = volume > 30 ? 30 : volume; // DFPlayer 的音量范围是 0-30
    send_cmd(0x06, volume);
}

// 指定文件夹播放
void DFPlayerMini_PlayFolder(uint8_t folder, uint8_t file)
{
    uint16_t param = ((folder & 0xFF) << 8) | (file & 0xFF);
    send_cmd(0x0F, param);
}
