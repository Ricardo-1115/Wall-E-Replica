#include "DFPlayerMini.h"

const int uart_buffer_size = (1024 * 2);
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

    uint8_t rx_buffer[19];
    uart_read_bytes(uart_num, &rx_buffer, sizeof(rx_buffer), 3000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "DFPlayer 初始化成功!");
    return ESP_OK;

    // ==========================================
    // // 补充逻辑：等待模块上电返回的初始化数据
    // // ==========================================

    // uint8_t rx_buffer[64]; // 接收缓冲区，大一点防止溢出
    // int rx_index = 0;          // 当前缓冲区的数据长度
    // int timeout_ms = 3500;     // 最大等待时间 3.5秒
    // int elapsed_ms = 0;
    // int delay_ms = 50; // 每次循环等待 50ms

    // // 在超时时间内循环读取并拼装数据
    // while (elapsed_ms < timeout_ms)
    // {
    //     // 读取串口中现有的数据，存入缓冲区剩余的空间
    //         if (bytes_read > 0) {
    //             rx_index += bytes_read;

    //             // 只要缓冲区里的数据大于等于 10 个字节，我们就去搜寻有效帧
    //             for (int i = 0; i <= rx_index - 10; i++) {
    //                 // 校验帧头(0x7E)、命令字(0x3F)和帧尾(0xEF)
    //                 if (rx_buffer[i] == 0x7E && rx_buffer[i+3] == 0x3F && rx_buffer[i+9] == 0xEF) {

    //                     uint8_t device_status = rx_buffer[i+6];
    //                     ESP_LOGI(TAG, "DFPlayer 初始化成功! 过滤了 %d 字节的干扰, 在线设备: 0x%02X", i, device_status);
    //                     uart_write_bytes(uart_num, "!!!", 4);
    //                     return ESP_OK; // 成功找到有效指令，完美退出
    //                 }
    //             }

    //             // 防止一直收到乱码导致缓冲区溢出
    //             // 如果缓冲区快满了，保留最后 9 个字节（防止截断半个有效帧），其余丢弃
    //             if (rx_index >= sizeof(rx_buffer) - 10) {
    //                 memmove(rx_buffer, &rx_buffer[rx_index - 9], 9);
    //                 rx_index = 9;
    //             }
    //         }
    //         elapsed_ms += delay_ms;
    //     }

    //     ESP_LOGE(TAG, "等待初始化指令超时或未收到正确的完整帧!");
    //     return ESP_FAIL;
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
void DFPlayerMini_set_volume(uint8_t volume)
{
    volume = volume > 30 ? 30 : volume; // DFPlayer 的音量范围是 0-30
    send_cmd(0x06, volume);
}

// 指定文件夹播放
void DFPlayerMini_play_folder(uint8_t folder, uint8_t file)
{
    uint16_t param = ((folder & 0xFF) << 8) | (file & 0xFF);
    send_cmd(0x0F, param);
}


/*
刚上电时出现 0xFF 或是 0x00 的乱码，在硬件上被称为 “上电毛刺”（Power-on Glitch）。这是因为 UART 的空闲状态（Idle State）
规定必须是高电平。但在刚通电的瞬间，DFPlayer 模块的内部芯片还没完全复位，此时它的 TX 引脚处于“高阻态”或“悬空”状态，电压是从
 0V 慢慢爬升到 3.3V 的。ESP32 极其敏锐，看到电压波动或者低电平，就以为是 UART 的“起始位（Start Bit）”来了，于是强行按 9600
  波特率去采样，结果就读出了一堆垃圾数据。在画电路图（PCB/原理图）时，你可以通过以下几个非常经典且成本极低的硬件手段来彻底绞
  杀这个干扰：1. 增加外部上拉电阻（最有效、最核心的方案）既然串口空闲时必须是高电平，而模块刚上电时自己“无力”维持高电平，那我
  们就用硬件帮它一把。做法：在 DFPlayer 模块的 TX 引脚与 ESP32 的 RX 引脚之间的信号线上，接一个 4.7kΩ 或 10kΩ 的电阻，另一
  端连接到 3.3V 电源（VCC）。原理：上电的一瞬间，哪怕 DFPlayer 还没反应过来，这个上拉电阻会瞬间把信号线死死拉到 3.3V 高电平
  。ESP32 看到的是稳稳的高电平，就不会误触发接收中断了。2. 添加串联电阻（阻尼/限流/防灌流）在 DFPlayer 的官方使用说明书的
  “4.1 串行接口”参考电路中，官方特别画出了串接电阻的接法 。做法：在 DFPlayer 的 TX 和 RX 线上，各串联一个 1kΩ 的电阻 。原
  理：说明书指出模块的串口为 3.3V 的 TTL 电平，如果在 5V 系统中建议串联 1K 阻值 。虽然你用的是 ESP32（也是 3.3V），但在
   3.3V 系统中串联一个 100Ω ~ 1kΩ 的电阻同样有极大好处：它可以与走线的寄生电容形成微小的 RC 滤波，吸收高频的上电毛刺尖峰。
   防止两个芯片上电速度不一致导致的“电流倒灌”（Latch-up 效应）。3. 严格的共地（GND）和电源去耦有时莫名其妙的乱码是因为地线
   （GND）电位不稳造成的。做法：确保 ESP32 的 GND 和 DFPlayer 的 GND 之间连接足够粗、足够短。去耦电容：在 DFPlayer 的
   VCC 和 GND 之间，尽可能靠近模块的引脚处，并联一个 10uF 的大电容和一个 104（0.1uF） 的小陶瓷电容。上电瞬间模块耗电会突
   变，这不仅能稳住电源，也能极大地减少串口引脚跟着电源一起“抖动”产生的毛刺。4. 彻底避开有下拉/LED的引脚结合咱们刚才排查出
   的问题，画板子时切记：做法：绝不使用 ESP32 上带有下拉电阻、接了按键、接了 LED（比如你之前的 GPIO2）的引脚作为 RX。原理
   ：外部的下拉元器件会和你的上拉电阻“打架”，削弱高电平，甚至把电平拉到 2V 左右的“灰色地带”，让串口极其容易受干扰。总结给
   你的电路图设计建议：在画原理图时，将 ESP32 的空闲引脚（例如 IO16、IO17）连到 DFPlayer 的 TX、RX。在这两条线上各串联一
   个 1kΩ 电阻，然后在 ESP32 的 RX 靠近芯片那一侧，接一个 10kΩ 电阻上拉到 3.3V。这样画出来的板子，通信稳如老狗，绝不会再
   有上电乱码！
*/