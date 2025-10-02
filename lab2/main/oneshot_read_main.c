#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "math.h"

const static char *TAG = "HEART_RATE";

/* ADC Macros */
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_0
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12

#define LOOP_DELAY 10              // ms
#define MAX_IBI_SAMPLES 10         // Number of recent IBIs for averaging

/* ADC Variables */
static int adc_raw;
static int voltage;
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);

/* IBI Variables */
TickType_t last_peak_tick = 0;
TickType_t last_bps_tick = 0;
int ibi_index = 0;
bool ibi_ready = false;
float ibi_average = 0.0f;

#define RMS_WINDOW_SIZE 50  // Adjust based on sampling rate
float rms_buffer[RMS_WINDOW_SIZE];
int rms_index = 0;
bool rms_filled = false;

float calculate_rms(float sample) {
    rms_buffer[rms_index] = sample;
    rms_index = (rms_index + 1) % RMS_WINDOW_SIZE;
    if (rms_index == 0) rms_filled = true;

    int count = rms_filled ? RMS_WINDOW_SIZE : rms_index;
    float sum_sq = 0;
    for (int i = 0; i < count; i++) {
        sum_sq += rms_buffer[i] * rms_buffer[i];
    }
    return sqrtf(sum_sq / count);
}

/*------------------------------------------
    Filters
------------------------------------------*/
float low_pass_filter(float input) {
    static float prev_output = 0;
    float a = 0.2f;
    float output = a * input + (1 - a) * prev_output;
    prev_output = output;
    return output;
}

float high_pass_filter(float input) {
    static float prev_input = 0;
    static float prev_output = 0;
    float a = 0.98f;
    float output = a * ((input - prev_input) + prev_output);
    prev_input = input;
    prev_output = output;
    return output;
}

/*------------------------------------------
    Peak Detection & IBI Processing
------------------------------------------*/
bool detect_peak(float current, float prev, float threshold) {
    static bool above_threshold = false;
    if (current > threshold && !above_threshold && current > prev) {
        above_threshold = true;
        return true;
    } else if (current < threshold) {
        above_threshold = false;
    }
    return false;
}

void process_peak(TickType_t current_tick) {
    if (last_peak_tick != 0) {
        TickType_t ibi_ticks = current_tick - last_peak_tick;
        float ibi_sec = ibi_ticks / (float)configTICK_RATE_HZ;

        // Moving average
        ibi_average = ibi_average * 0.9f + ibi_sec * 0.1f;

        // Calculate BPM when detecting a peak
        float bpm = 60.0f / ibi_average;
        if (bpm > 0) {
            ESP_LOGI(TAG, "Estimated BPM: %.1f", bpm);
        }
    }
    last_peak_tick = current_tick;
}

/*------------------------------------------
    Main Application
------------------------------------------*/
void app_main(void) {
    /* ADC Init */
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = EXAMPLE_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));

    /* ADC Calibration */
    adc_cali_handle_t adc1_cali_handle = NULL;
    bool do_calibration = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN, &adc1_cali_handle);

    float prev_filtered = 0.0f;

    while (1) {
        TickType_t current_tick = xTaskGetTickCount();

        /* Read ADC */
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw));
        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage));
        } else {
            voltage = adc_raw;
        }

        /* Filtering */
        float signal_hp = high_pass_filter(voltage);
        float signal_lp = low_pass_filter(signal_hp);

        // Update dynamic threshold
        float threshold = calculate_rms(signal_lp) * 1.5f;  // scale factor

        /* Peak detection & IBI processing */
        if (detect_peak(signal_lp, prev_filtered, threshold)) {
            process_peak(current_tick);
        }
        prev_filtered = signal_lp;

        /*ESP_LOGI(TAG, "Señal filtrada: %.2f mV", signal_lp);
        ESP_LOGI(TAG, "Umbral dinámico: %.2f mV", threshold);*/

        /* Delay to maintain sampling rate */
        TickType_t elapsed = xTaskGetTickCount() - current_tick;
        if (elapsed < pdMS_TO_TICKS(LOOP_DELAY)) {
            vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY) - elapsed);
        } else {
            taskYIELD();
        }
    }

    /* Tear down */
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
    if (do_calibration) {
        example_adc_calibration_deinit(adc1_cali_handle);
    }
}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
        *out_handle = handle;
        ESP_LOGI(TAG, "ADC Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "ADC Calibration failed");
    }

    return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (handle) {
        ESP_LOGI(TAG, "Deregistering Curve Fitting Calibration");
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
    }
#endif
}