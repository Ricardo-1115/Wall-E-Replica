#include <u8g2.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp32_hw_i2c.h"

extern u8g2_t u8g2;

/**
 * Functions to draw each of the battery level bars
 */
static void drawBattery(u8g2_t *u8g2, uint8_t battery){
    // 参数检查 (限制为10，20，..., 100)
    if((battery % 10) != 0 || battery > 100) return;
    
    uint8_t w = (battery == 10) ? 16 : 7;
    u8g2_DrawBox(u8g2, (100 - battery) / 10 * 12, 0, w, 40);
}


/**
 * Draw the sun icon on the display
 */
static void drawSun(u8g2_t *u8g2)
{
    // // === original version ===
    // u8g2_DrawDisc(u8g2, 20, 55, 3, U8G2_DRAW_ALL);
    // u8g2_DrawLine(u8g2, 20, 50, 20, 46);
    // u8g2_DrawLine(u8g2, 20, 60, 20, 64);
    // u8g2_DrawLine(u8g2, 15, 55, 11, 55);
    // u8g2_DrawLine(u8g2, 25, 55, 29, 55);
    // u8g2_DrawLine(u8g2, 22, 54, 25, 47);
    // u8g2_DrawLine(u8g2, 25, 53, 29, 50);
    // u8g2_DrawLine(u8g2, 16, 53, 12, 50);
    // u8g2_DrawLine(u8g2, 18, 51, 16, 47);
    // u8g2_DrawLine(u8g2, 16, 58, 12, 60);
    // u8g2_DrawLine(u8g2, 18, 60, 16, 63);
    // u8g2_DrawLine(u8g2, 25, 58, 29, 60);
    // u8g2_DrawLine(u8g2, 22, 60, 25, 63);

    // 优化版：中心(20,55)，8条45°间隔对称射线
    u8g2_DrawDisc(u8g2, 20, 55, 3, U8G2_DRAW_ALL);
    u8g2_DrawLine(u8g2, 20, 51, 20, 46);   // 上
    u8g2_DrawLine(u8g2, 20, 59, 20, 64);   // 下
    u8g2_DrawLine(u8g2, 16, 55, 11, 55);   // 左
    u8g2_DrawLine(u8g2, 24, 55, 29, 55);   // 右
    u8g2_DrawLine(u8g2, 17, 52, 13, 48);   // 左上
    u8g2_DrawLine(u8g2, 23, 52, 27, 48);   // 右上
    u8g2_DrawLine(u8g2, 17, 58, 13, 62);   // 左下
    u8g2_DrawLine(u8g2, 23, 58, 27, 62);   // 右下
}

/**
 * Draw battery level on the display
 *
 * @param u8g2 Pointer to the U8G2 display object
 * @param batlevel The current battery percentage
 */
static void displayLevel(u8g2_t *u8g2, int batlevel)
{
    u8g2_FirstPage(u8g2);
    do
    {
        u8g2_SetDrawColor(u8g2, 1);
        drawSun(u8g2);

        // Scale to 50% as the battery should not drop below that anyway
        drawBattery(u8g2, 10);
        if (batlevel > 55)
            drawBattery(u8g2, 20);
        if (batlevel > 60)
            drawBattery(u8g2, 30);
        if (batlevel > 65)
            drawBattery(u8g2, 40);
        if (batlevel > 70)
            drawBattery(u8g2, 50);
        if (batlevel > 75)
            drawBattery(u8g2, 60);
        if (batlevel > 80)
            drawBattery(u8g2, 70);
        if (batlevel > 85)
            drawBattery(u8g2, 80);
        if (batlevel > 90)
            drawBattery(u8g2, 90);
        if (batlevel > 95)
            drawBattery(u8g2, 100);
    } while (u8g2_NextPage(u8g2));
}


void example_demo_oled_ui(void)
{
    // // Demo: Display battery levels from 50% to 100%
    // int battery_levels[] = {50, 60, 70, 80, 90, 100};
    // int num_levels = sizeof(battery_levels) / sizeof(battery_levels[0]);

    // for (int i = 0; i < num_levels; i++)
    // {
    //     displayLevel(&u8g2, battery_levels[i]);
    //     vTaskDelay(2000 / portTICK_PERIOD_MS); // Wait 2 seconds
    // }

    u8g2_FirstPage(&u8g2);
    do{
        u8g2_SetFont(&u8g2, u8g2_font_micro_tr);
        u8g2_DrawStr(&u8g2, 0, 5, "SOLAR CHARGE LEVEL");
    }while(u8g2_NextPage(&u8g2));

}
