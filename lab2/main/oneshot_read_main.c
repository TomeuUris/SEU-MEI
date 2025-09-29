#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

const static char *TAG = "HEART_RATE";

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_0
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12

static int adc_raw;
static int voltage;
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);

// -------------------------
// Low-pass filter (pasa-bajos)
// -------------------------
float low_pass_filter(float input) {
    static float prev_output = 0;
    float a = 0.67f;
    float output = a * input + (1 - a) * prev_output;
    prev_output = output;
    return output;
}

// -------------------------
// High-pass filter (pasa-altos)
// -------------------------
float high_pass_filter(float input) {
    static float prev_input = 0;
    static float prev_output = 0;
    float a = 0.76f;
    float output = a * ((input - prev_input) + prev_output);
    prev_input = input;
    prev_output = output;
    return output;
}

// -------------------------
// Detect peaks for BPM calculation
// -------------------------
bool detect_peak(float current, float prev, float threshold) {
    static bool above_threshold = false;

    if (current > threshold && !above_threshold && current > prev) {
        above_threshold = true;
        return true; // Pico detectado
    } else if (current < threshold) {
        above_threshold = false;
    }
    return false;
}

void app_main(void)
{
    //-------------ADC1 Init---------------//
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .atten = EXAMPLE_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));

    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc1_cali_handle = NULL;
    bool do_calibration = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN, &adc1_cali_handle);

    // Variables para detección de picos y BPM
    float prev_filtered = 0.0f;
    int peak_count = 0;
    TickType_t start_time = xTaskGetTickCount();

    // Threshold
    float threshold = 20.0f; 

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw));
        ESP_LOGI(TAG, "ADC%d Channel%d Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw);

        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage));
        } else {
            voltage = adc_raw; // fallback
        }

        // Filtrado
        float signal_hp = high_pass_filter(voltage);   // Quita DC
        float signal_lp = low_pass_filter(signal_hp);  // Suaviza ruido

        // Detección de picos
        if (detect_peak(signal_lp, prev_filtered, threshold)) {
            peak_count++;
        }
        prev_filtered = signal_lp;

        // Calcular BPM cada 5 segundos
        TickType_t current_time = xTaskGetTickCount();
        float elapsed_sec = (current_time - start_time) / (float)configTICK_RATE_HZ;
        if (elapsed_sec >= 5.0f) {
            float bpm = (peak_count / elapsed_sec) * 60.0f;
            ESP_LOGI(TAG, "BPM estimado: %.1f", bpm);
            peak_count = 0;
            start_time = current_time;
        }
        ESP_LOGI(TAG, "Señal filtrada: %.2f mV", signal_lp);

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Tear Down
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
