#include <stdio.h>
#include <string.h>
#include <u8g2.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp32_hw_i2c.h"
#include "display.h"

extern u8g2_t u8g2;

/* ================================================================== */
/*  Battery bar drawing (horizontal bars, stacked right-to-left)       */
/* ================================================================== */

/**
 * Draw a single battery level bar
 * @param u8g2    Display handle
 * @param battery Level in 10% increments (10, 20, ..., 100)
 */
static void drawBattery(u8g2_t *u8g2, uint8_t battery)
{
    if ((battery % 10) != 0 || battery > 100) return;

    uint8_t w = (battery == 10) ? 16 : 7;
    u8g2_DrawBox(u8g2, (100 - battery) / 10 * 12, 0, w, 40);
}



/* ================================================================== */
/*  Sun icon                                                            */
/* ================================================================== */
static void drawSun(u8g2_t *u8g2)
{
    u8g2_DrawDisc(u8g2, 20, 55, 3, U8G2_DRAW_ALL);
    u8g2_DrawLine(u8g2, 20, 51, 20, 46);   /* up */
    u8g2_DrawLine(u8g2, 20, 59, 20, 64);   /* down */
    u8g2_DrawLine(u8g2, 16, 55, 11, 55);   /* left */
    u8g2_DrawLine(u8g2, 24, 55, 29, 55);   /* right */
    u8g2_DrawLine(u8g2, 17, 52, 13, 48);   /* up-left */
    u8g2_DrawLine(u8g2, 23, 52, 27, 48);   /* up-right */
    u8g2_DrawLine(u8g2, 17, 58, 13, 62);   /* down-left */
    u8g2_DrawLine(u8g2, 23, 58, 27, 62);   /* down-right */
}

/* ================================================================== */
/*  Public API — combined battery + solar display                      */
/* ================================================================== */

void display_battery_show(uint8_t percent, uint32_t voltage_mv)
{
    char line[32];

    u8g2_FirstPage(&u8g2);
    do {
        u8g2_SetDrawColor(&u8g2, 1);

        /* --- Battery bars (upper area) --- */
        drawBattery(&u8g2, 10);
        if (percent > 55) drawBattery(&u8g2, 20);
        if (percent > 60) drawBattery(&u8g2, 30);
        if (percent > 65) drawBattery(&u8g2, 40);
        if (percent > 70) drawBattery(&u8g2, 50);
        if (percent > 75) drawBattery(&u8g2, 60);
        if (percent > 80) drawBattery(&u8g2, 70);
        if (percent > 85) drawBattery(&u8g2, 80);
        if (percent > 90) drawBattery(&u8g2, 90);
        if (percent > 95) drawBattery(&u8g2, 100);

        /* --- Sun icon (lower-left) --- */
        drawSun(&u8g2);

        /* --- Text: percentage + voltage --- */
        u8g2_SetFont(&u8g2, u8g2_font_5x7_tr);
        snprintf(line, sizeof(line), "%u%%", (unsigned)percent);
        u8g2_DrawStr(&u8g2, 45, 10, line);

        if (voltage_mv > 0) {
            snprintf(line, sizeof(line), "%u.%02uV",
                     (unsigned)(voltage_mv / 1000),
                     (unsigned)((voltage_mv % 1000) / 10));
            u8g2_DrawStr(&u8g2, 45, 22, line);
        }

        u8g2_SetFont(&u8g2, u8g2_font_micro_tr);
        u8g2_DrawStr(&u8g2, 0, 64, "SOLAR CHARGE");

    } while (u8g2_NextPage(&u8g2));
}

/* ================================================================== */
/*  Legacy demo                                                        */
/* ================================================================== */
void example_demo_oled_ui(void)
{
    u8g2_FirstPage(&u8g2);
    do {
        u8g2_SetFont(&u8g2, u8g2_font_micro_tr);
        u8g2_DrawStr(&u8g2, 0, 5, "SOLAR CHARGE LEVEL");
    } while (u8g2_NextPage(&u8g2));
}
