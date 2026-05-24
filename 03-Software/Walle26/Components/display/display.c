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
/*  Battery bar drawing (vertical bars, stacked bottom-to-top)         */
/* ================================================================== */

/**
 * Draw a single battery level bar
 * @param u8g2    Display handle
 * @param battery Level in 10% increments (10, 20, ..., 100)
 *
 * Bars are drawn from the bottom-right corner, extending upward.
 * Each bar extends 40 pixels leftward from the right edge.
 *
 * Coordinates assume U8G2_R1 (64x128 portrait).
 */
static void drawBattery(u8g2_t *u8g2, uint8_t battery)
{
    if ((battery % 10) != 0 || battery > 100) return;

    uint8_t h = (battery == 10) ? 16 : 7;
    uint8_t x = 24;              /* 40px from right edge: 63-40+1 */
    uint8_t w = 40;
    uint8_t y = 126 - (battery / 10 - 1) * 12 - h + 1;
    u8g2_DrawBox(u8g2, x, y, w, h);
}



/* ================================================================== */
/*  Sun icon                                                            */
/* ================================================================== */
static void drawSun(u8g2_t *u8g2)
{
    u8g2_DrawDisc(u8g2, 10, 20, 3, U8G2_DRAW_ALL);
    u8g2_DrawLine(u8g2, 10, 16, 10, 11);    /* up */
    u8g2_DrawLine(u8g2, 10, 24, 10, 29);    /* down */
    u8g2_DrawLine(u8g2, 6, 20, 1, 20);      /* left */
    u8g2_DrawLine(u8g2, 14, 20, 19, 20);    /* right */
    u8g2_DrawLine(u8g2, 7, 17, 3, 13);      /* up-left */
    u8g2_DrawLine(u8g2, 13, 17, 17, 13);    /* up-right */
    u8g2_DrawLine(u8g2, 7, 23, 3, 27);      /* down-left */
    u8g2_DrawLine(u8g2, 13, 23, 17, 27);    /* down-right */
}

/* ================================================================== */
/*  Public API — combined battery + solar display                      */
/* ================================================================== */

void display_battery_show(uint8_t percent, uint32_t voltage_mv)
{
    (void)voltage_mv;

    u8g2_FirstPage(&u8g2);
    do {
        u8g2_SetDrawColor(&u8g2, 1);

        /* --- "SOLAR CHARGE LEVEL" at top, horizontal --- */
        u8g2_SetFont(&u8g2, u8g2_font_micro_tr);
        u8g2_DrawStr(&u8g2, 0, 8, "SOLAR CHARGE LEVEL");

        /* --- Sun icon below title --- */
        drawSun(&u8g2);

        /* --- Battery bars (bottom-right, extending upward) --- */
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
        u8g2_DrawStr(&u8g2, 0, 8, "SOLAR CHARGE LEVEL");
    } while (u8g2_NextPage(&u8g2));
}
