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
static void drawBatt10(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 108, 0, 16, 40);
}

static void drawBatt20(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 96, 0, 7, 40);
}

static void drawBatt30(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 84, 0, 7, 40);
}

static void drawBatt40(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 72, 0, 7, 40);
}

static void drawBatt50(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 60, 0, 7, 40);
}

static void drawBatt60(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 48, 0, 7, 40);
}

static void drawBatt70(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 36, 0, 7, 40);
}

static void drawBatt80(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 24, 0, 7, 40);
}

static void drawBatt90(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 12, 0, 7, 40);
}

static void drawBatt100(u8g2_t *u8g2)
{
    u8g2_DrawBox(u8g2, 0, 0, 7, 40);
}

/**
 * Draw the sun icon on the display
 */
static void drawSun(u8g2_t *u8g2)
{
    u8g2_DrawDisc(u8g2, 20, 55, 3, U8G2_DRAW_ALL);
    u8g2_DrawLine(u8g2, 20, 50, 20, 46);
    u8g2_DrawLine(u8g2, 20, 60, 20, 64);
    u8g2_DrawLine(u8g2, 15, 55, 11, 55);
    u8g2_DrawLine(u8g2, 25, 55, 29, 55);
    u8g2_DrawLine(u8g2, 22, 54, 25, 47);
    u8g2_DrawLine(u8g2, 25, 53, 29, 50);
    u8g2_DrawLine(u8g2, 16, 53, 12, 50);
    u8g2_DrawLine(u8g2, 18, 51, 16, 47);
    u8g2_DrawLine(u8g2, 16, 58, 12, 60);
    u8g2_DrawLine(u8g2, 18, 60, 16, 63);
    u8g2_DrawLine(u8g2, 25, 58, 29, 60);
    u8g2_DrawLine(u8g2, 22, 60, 25, 63);
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
        drawBatt10(u8g2);
        if (batlevel > 55)
            drawBatt20(u8g2);
        if (batlevel > 60)
            drawBatt30(u8g2);
        if (batlevel > 65)
            drawBatt40(u8g2);
        if (batlevel > 70)
            drawBatt50(u8g2);
        if (batlevel > 75)
            drawBatt60(u8g2);
        if (batlevel > 80)
            drawBatt70(u8g2);
        if (batlevel > 85)
            drawBatt80(u8g2);
        if (batlevel > 90)
            drawBatt90(u8g2);
        if (batlevel > 95)
            drawBatt100(u8g2);
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
