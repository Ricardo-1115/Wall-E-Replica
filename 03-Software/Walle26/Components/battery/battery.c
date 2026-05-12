#include <string.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "battery.h"

/* ------------------------------------------------------------------ */
/*  Hardware config                                                    */
/* ------------------------------------------------------------------ */
#define BATTERY_ADC_UNIT        ADC_UNIT_1
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_0         /* ADC1_CH0 = GPIO1 */
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_2_5       /* 0 ~ 1250 mV */
#define BATTERY_ADC_BITWIDTH    ADC_BITWIDTH_12        /* 0 ~ 4095 */

/* Voltage divider: R1=10k, R2=1k, ratio = 1/11
 * V_bat = V_adc * (R1+R2)/R2 = V_adc * 11  */
#define DIVIDER_RATIO           11

/* ------------------------------------------------------------------ */
/*  Filtering & timing                                                 */
/* ------------------------------------------------------------------ */
#define SAMPLE_INTERVAL_MS      1000
#define FILTER_TAP_NUM          8  

/* ------------------------------------------------------------------ */
/*  3S LiPo voltage → percentage lookup table                          */
/* ------------------------------------------------------------------ */
static const int lipo_percent_map[][2] = {
    {12600, 100}, {12300, 90},  {12000, 80},
    {11700, 70},  {11400, 60},  {11100, 50},
    {10800, 40},  {10500, 30},  {10200, 20},
    { 9600, 10},  { 9000,  0},
};
#define LIPO_MAP_ENTRIES (sizeof(lipo_percent_map) / sizeof(lipo_percent_map[0]))

/* ------------------------------------------------------------------ */
/*  Module state                                                       */
/* ------------------------------------------------------------------ */
static const char *TAG = "BATTERY";

static adc_oneshot_unit_handle_t s_adc_handle = NULL; 
static adc_cali_handle_t s_cali_handle = NULL;

static int32_t s_filter_buf[FILTER_TAP_NUM];
static uint8_t  s_filter_idx = 0;
static uint32_t s_voltage_mv = 0;
static uint8_t  s_percentage  = 0;

/* ------------------------------------------------------------------ */
/*  LiPo voltage → percentage (linear interpolation)                   */
/* ------------------------------------------------------------------ */
static uint8_t voltage_to_percent(uint32_t mv)
{
    if (mv >= lipo_percent_map[0][0]) return 100;
    if (mv <= lipo_percent_map[LIPO_MAP_ENTRIES - 1][0]) return 0;

    for (int i = 0; i < LIPO_MAP_ENTRIES - 1; i++) {
        if (mv <= lipo_percent_map[i][0] && mv > lipo_percent_map[i + 1][0]) {
            int v_high = lipo_percent_map[i][0];
            int v_low  = lipo_percent_map[i + 1][0];
            int p_high = lipo_percent_map[i][1];
            int p_low  = lipo_percent_map[i + 1][1];
            return p_low + (uint32_t)(p_high - p_low) * (mv - v_low) / (v_high - v_low);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  ADC calibration init                                               */
/* ------------------------------------------------------------------ */
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "Calibration scheme: Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = BATTERY_ADC_BITWIDTH,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "Calibration scheme: Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = BATTERY_ADC_BITWIDTH,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    if (calibrated) {
        ESP_LOGI(TAG, "Calibration success");
    } else {
        ESP_LOGW(TAG, "eFuse not burnt, skip calibration (fallback to raw)");
    }
    return calibrated;
}

/* ------------------------------------------------------------------ */
/*  Sample & update filtered voltage                                    */
/* ------------------------------------------------------------------ */
static void sample_battery(void)
{
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        return;
    }

    int cal_mv = 0;
    if (s_cali_handle != NULL) {
        ret = adc_cali_raw_to_voltage(s_cali_handle, raw, &cal_mv);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Raw to voltage failed: %s", esp_err_to_name(ret));
            cal_mv = 0;
        }
    }

    /* Fallback: manual conversion when calibration unavailable */
    if (cal_mv == 0) {
        cal_mv = (int64_t)raw * 1467 / 4095;    // Vref 出厂设定为 1100 mV, 2.5dB 衰减实际乘以 1.33，1100*1.33=1467
    }

    /* Battery-side voltage = ADC pin voltage × 11 */
    uint32_t bat_mv = cal_mv * DIVIDER_RATIO;

    /* Moving-average filter (ring buffer) */
    s_filter_buf[s_filter_idx] = (int32_t)bat_mv;
    s_filter_idx = (s_filter_idx + 1) % FILTER_TAP_NUM;

    int64_t sum = 0;
    for (int i = 0; i < FILTER_TAP_NUM; i++) sum += s_filter_buf[i];
    s_voltage_mv = (uint32_t)(sum / FILTER_TAP_NUM);
    s_percentage = voltage_to_percent(s_voltage_mv);
}

/* ------------------------------------------------------------------ */
/*  Background task                                                     */
/* ------------------------------------------------------------------ */
static void battery_task(void *arg)
{
    /* Prime filter buffer */
    for (int i = 0; i < FILTER_TAP_NUM; i++) {
        sample_battery();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
        sample_battery();
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
void battery_init(void)
{
    /* Oneshot ADC unit config */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc_handle));

    /* Oneshot channel config */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = BATTERY_ADC_ATTEN,
        .bitwidth = BATTERY_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg));

    /* Calibration init */
    adc_calibration_init(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, BATTERY_ADC_ATTEN, &s_cali_handle);

    /* Reset filter */
    memset(s_filter_buf, 0, sizeof(s_filter_buf));
    s_filter_idx = 0;
    s_voltage_mv = 0;
    s_percentage = 0;

    /* Create background sampling task (core 1, low priority) */
    xTaskCreatePinnedToCore(battery_task, "battery", 3072, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Battery monitoring initialized (ADC1_CH0, 2.5dB, 12-bit)");
}

uint32_t battery_get_voltage_mv(void)
{
    return s_voltage_mv;
}

uint8_t battery_get_percentage(void)
{
    return s_percentage;
}
