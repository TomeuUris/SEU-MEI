/* Caso B: Toggle LED solo con pulsación larga (>0.5s)
   Basado en el código de referencia proporcionado
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "case_b";

#define LED_GPIO 4
#define BUTTON_GPIO 0
#define LONG_PRESS_TIME_MS 500

// Estados de la máquina de estados
typedef enum {
    STATE_IDLE,
    STATE_BUTTON_PRESSED,
    STATE_BUTTON_RELEASED
} button_state_t;

static uint8_t s_led_state = 0;
static button_state_t current_state = STATE_IDLE;
static TickType_t press_start_time = 0;

static void toggle_led(void)
{
    gpio_set_level(LED_GPIO, s_led_state);
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Configurando LED en GPIO%d", LED_GPIO);
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

static void configure_button(void)
{
    ESP_LOGI(TAG, "Configurando botón en GPIO%d", BUTTON_GPIO);
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_PULLDOWN);
}

void app_main(void)
{
    configure_led();
    configure_button();

    ESP_LOGI(TAG, "Iniciando máquina de estados - Caso B (pulsación larga)");
    
    while (1) {
        uint8_t button_level = gpio_get_level(BUTTON_GPIO);
        TickType_t current_time = xTaskGetTickCount();
        
        switch (current_state) {
            case STATE_IDLE:
                if (button_level == 1) {
                    // Botón presionado, iniciar conteo de tiempo
                    current_state = STATE_BUTTON_PRESSED;
                    press_start_time = current_time;
                    ESP_LOGI(TAG, "Botón presionado, iniciando conteo...");
                }
                break;
                
            case STATE_BUTTON_PRESSED:
                if (button_level == 0) {
                    // Botón soltado antes del tiempo mínimo
                    current_state = STATE_IDLE;
                    ESP_LOGI(TAG, "Botón soltado demasiado pronto");
                } else {
                    // Verificar si ha pasado el tiempo suficiente
                    TickType_t elapsed = (current_time - press_start_time) * portTICK_PERIOD_MS;
                    if (elapsed >= LONG_PRESS_TIME_MS) {
                        // Pulsación válida - cambiar a estado de espera de liberación
                        current_state = STATE_BUTTON_RELEASED;
                        s_led_state = !s_led_state;
                        toggle_led();
                        ESP_LOGI(TAG, "Pulsación larga detectada! LED: %s", s_led_state ? "ON" : "OFF");
                    }
                }
                break;
                
            case STATE_BUTTON_RELEASED:
                if (button_level == 0) {
                    // Botón liberado, volver al estado inicial
                    current_state = STATE_IDLE;
                    ESP_LOGI(TAG, "Botón liberado, volviendo al estado inicial");
                }
                break;
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}