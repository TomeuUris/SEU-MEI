/* Caso C: Pulsación larga (>0.5s) activa intermitencia rápida por 10s
   Nueva pulsación larga cancela la intermitencia
   Basado en ESP32-C6, sin interrupciones ni threads
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "case_c";

#define LED_GPIO 4
#define BUTTON_GPIO 0
#define LONG_PRESS_TIME_MS 500
#define BLINK_DURATION_MS 10000    // 10 segundos de intermitencia
#define BLINK_PERIOD_MS 200        // Periodo de intermitencia (100ms ON, 100ms OFF)

// Estados de la máquina de estados
typedef enum {
    STATE_IDLE,
    STATE_BUTTON_PRESSED,
    STATE_BUTTON_RELEASED,
    STATE_BLINKING,
    STATE_BLINKING_BUTTON_PRESSED,
    STATE_BLINKING_BUTTON_RELEASED
} button_state_t;

static uint8_t s_led_state = 0;
static button_state_t current_state = STATE_IDLE;
static TickType_t press_start_time = 0;
static TickType_t blink_start_time = 0;
static TickType_t last_blink_time = 0;

static void set_led(uint8_t state)
{
    gpio_set_level(LED_GPIO, state);
    s_led_state = state;
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Configurando LED en GPIO%d", LED_GPIO);
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    set_led(0); // Inicializar LED apagado
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

    ESP_LOGI(TAG, "Iniciando máquina de estados - Caso C (intermitencia con pulsación larga)");
    
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
                        // Pulsación válida - iniciar intermitencia INMEDIATAMENTE
                        current_state = STATE_BUTTON_RELEASED;
                        blink_start_time = current_time;
                        last_blink_time = current_time;
                        set_led(1); // Empezar con LED encendido
                        ESP_LOGI(TAG, "Pulsación larga detectada! Iniciando intermitencia inmediatamente");
                    }
                }
                break;
                
            case STATE_BUTTON_RELEASED:
                // Verificar primero si han pasado 10 segundos
                TickType_t blink_elapsed = (current_time - blink_start_time) * portTICK_PERIOD_MS;
                if (blink_elapsed >= BLINK_DURATION_MS) {
                    // Terminar intermitencia sin importar el estado del botón
                    current_state = STATE_IDLE;
                    set_led(0);
                    ESP_LOGI(TAG, "Intermitencia terminada después de 10 segundos, LED apagado");
                } else if (button_level == 0) {
                    // Botón liberado, continuar intermitencia
                    current_state = STATE_BLINKING;
                    ESP_LOGI(TAG, "Botón liberado, continuando intermitencia");
                } else {
                    // Botón aún presionado, continuar intermitencia
                    TickType_t blink_period_elapsed = (current_time - last_blink_time) * portTICK_PERIOD_MS;
                    if (blink_period_elapsed >= BLINK_PERIOD_MS / 2) {
                        s_led_state = !s_led_state;
                        set_led(s_led_state);
                        last_blink_time = current_time;
                    }
                }
                break;
                
            case STATE_BLINKING:
                // Verificar si el botón ha sido presionado durante la intermitencia
                if (button_level == 1) {
                    current_state = STATE_BLINKING_BUTTON_PRESSED;
                    press_start_time = current_time;
                    ESP_LOGI(TAG, "Botón presionado durante intermitencia...");
                } else {
                    // Verificar si han pasado 10 segundos
                    TickType_t blink_elapsed = (current_time - blink_start_time) * portTICK_PERIOD_MS;
                    if (blink_elapsed >= BLINK_DURATION_MS) {
                        // Terminar intermitencia
                        current_state = STATE_IDLE;
                        set_led(0); // Apagar LED
                        ESP_LOGI(TAG, "Intermitencia terminada, LED apagado");
                    } else {
                        // Continuar intermitencia
                        TickType_t blink_period_elapsed = (current_time - last_blink_time) * portTICK_PERIOD_MS;
                        if (blink_period_elapsed >= BLINK_PERIOD_MS / 2) { // Cambiar cada 100ms
                            s_led_state = !s_led_state;
                            set_led(s_led_state);
                            last_blink_time = current_time;
                        }
                    }
                }
                break;
                
            case STATE_BLINKING_BUTTON_PRESSED:
                if (button_level == 0) {
                    // Botón soltado antes del tiempo mínimo, continuar intermitencia
                    current_state = STATE_BLINKING;
                    ESP_LOGI(TAG, "Botón soltado demasiado pronto, continuando intermitencia");
                } else {
                    // Verificar si ha pasado el tiempo suficiente para cancelar
                    TickType_t elapsed = (current_time - press_start_time) * portTICK_PERIOD_MS;
                    if (elapsed >= LONG_PRESS_TIME_MS) {
                        // Pulsación larga válida - cancelar intermitencia
                        current_state = STATE_BLINKING_BUTTON_RELEASED;
                        ESP_LOGI(TAG, "Pulsación larga detectada! Cancelando intermitencia...");
                    } else {
                        // Continuar intermitencia mientras se verifica la pulsación
                        TickType_t blink_period_elapsed = (current_time - last_blink_time) * portTICK_PERIOD_MS;
                        if (blink_period_elapsed >= BLINK_PERIOD_MS / 2) {
                            s_led_state = !s_led_state;
                            set_led(s_led_state);
                            last_blink_time = current_time;
                        }
                    }
                }
                break;
                
            case STATE_BLINKING_BUTTON_RELEASED:
                if (button_level == 0) {
                    // Botón liberado, cancelar intermitencia
                    current_state = STATE_IDLE;
                    set_led(0); // Apagar LED
                    ESP_LOGI(TAG, "Botón liberado, intermitencia cancelada, LED apagado");
                }
                break;
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}