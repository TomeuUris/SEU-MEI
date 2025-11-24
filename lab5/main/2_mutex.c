#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <stdio.h>

// Define CONFIG_FREERTOS_HZ if not defined (typical ESP32 value is 1000Hz)
#ifndef CONFIG_FREERTOS_HZ
#define CONFIG_FREERTOS_HZ 1000
#endif

// Definiciones de GPIO/LEDs (GPIOs válidos para ESP32-C6)
#define LED1_GPIO 1
#define LED2_GPIO 2
#define LED3_GPIO 3

// Implementación de las funciones de control de hardware
void led_init(int gpio) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << gpio),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio, 0); // Iniciar en OFF
    printf("LED GPIO %d initialized\n", gpio);
}

void led_set_level(int gpio, int level) {
    gpio_set_level(gpio, level);
    printf("LED GPIO %d set to level %d\n", gpio, level);
}

// Tarea independiente para LED1
void led_blink_task_1(void *pvParameters) {
    led_init(LED1_GPIO); // Inicializar LED1
    while (1) {
        // Intermitencia de 0.3 segundos (300 ms)
        led_set_level(LED1_GPIO, 1); // ON
        vTaskDelay(pdMS_TO_TICKS(300));
        led_set_level(LED1_GPIO, 0); // OFF
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// -----------------------------------------------------
// VERSION A: MUTEX
// -----------------------------------------------------

SemaphoreHandle_t shared_mutex;

void led_alternate_task_2_3_mutex(void *pvParameters) {
    led_init(LED2_GPIO); // Inicializar LED2 y LED3
    led_init(LED3_GPIO);
    
    while (1) {
        // 1. Intentar adquirir el mutex
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            // Sección Crítica: Alternancia de 1 segundo (1000 ms)
            // Asegura que LED2 y LED3 no se manipulen simultáneamente
            
            printf("[Mutex] LED2 ON / LED3 OFF\n");
            led_set_level(LED2_GPIO, 1);
            led_set_level(LED3_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));

            printf("[Mutex] LED2 OFF / LED3 ON\n");
            led_set_level(LED2_GPIO, 0);
            led_set_level(LED3_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // 2. Liberar el mutex
            xSemaphoreGive(shared_mutex);
        }
    }
}

void app_main() {
    // Crear el Mutex (soporta Priority Inheritance)
    shared_mutex = xSemaphoreCreateMutex();
    
    if (shared_mutex != NULL) {
        // Tarea 1: Intermitencia (Independiente)
        xTaskCreate(led_blink_task_1, "LED1_Blink", 2048, NULL, 5, NULL);
        
        // Tarea 2/3: Alternancia (Protegida por Mutex)
        xTaskCreate(led_alternate_task_2_3_mutex, "LED23_Alternate", 2048, NULL, 5, NULL);
    }
}
