#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_cpu.h"       // Necesario para esp_cpu_get_cycle_count()
#include "esp_log.h"       // Necesario para usar ESP_LOGI
#include <stdio.h>

// Definición de la etiqueta de logging para el overhead
static const char *TAG = "OVERHEAD_MEASURE";

// --- Definiciones de Hardware ---
#define LED1_GPIO 1
#define LED2_GPIO 2
#define LED3_GPIO 3

// Parámetros de sincronización y medición
SemaphoreHandle_t shared_mutex; // Mutex requerido por el Apartado 2
static uint32_t start_cycles = 0;
static const float CPU_FREQ_MHZ = 160.0; 

// --- Funciones de control de hardware ---
void led_init(int gpio) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE, .mode = GPIO_MODE_OUTPUT, 
        .pin_bit_mask = (1ULL << gpio), .pull_down_en = 0, .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio, 0); 
    printf("Hardware: LED GPIO %d initialized\n", gpio);
}

void led_set_level(int gpio, int level) {
    gpio_set_level(gpio, level);
    printf("Hardware: -> GPIO %d set to %d.\n", gpio, level); 
}

// -----------------------------------------------------------------
// TAREAS DEL APARTADO 2 (Prioridad 5) - Exclusión Mutua
// -----------------------------------------------------------------

// Tarea 1: Intermitencia para LED1 (0.3s)
void led_blink_task_1(void *pvParameters) {
    led_init(LED1_GPIO);
    while (1) {
        printf("[T1] LED1 ON (300 ms)\n");
        led_set_level(LED1_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(300));

        printf("[T1] LED1 OFF (300 ms)\n");
        led_set_level(LED1_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// Tarea 2: Controla el Ciclo 1. Compite por el Mutex.
void led_control_task_2(void *pvParameters) {
    led_init(LED2_GPIO); 
    while (1) {
        // 1. Intentar adquirir el Mutex
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            // SECCIÓN CRÍTICA: Ciclo 1: LED2 ON / LED3 OFF
            printf("[T2] CRÍTICA: LED2 ON / LED3 OFF (1000ms)\n");
            led_set_level(LED2_GPIO, 1);
            led_set_level(LED3_GPIO, 0);
            
            // Retraso DENTRO del Mutex (fuerza la retención por 1 segundo)
            vTaskDelay(pdMS_TO_TICKS(1000)); 

            // 2. Liberar el Mutex
            xSemaphoreGive(shared_mutex);
        }
        // Espera corta para evitar acaparamiento si la otra tarea libera el Mutex
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

// Tarea 3: Controla el Ciclo 2. Compite por el Mutex.
void led_control_task_3(void *pvParameters) {
    led_init(LED3_GPIO); 
    while (1) {
        // 1. Intentar adquirir el Mutex
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            // SECCIÓN CRÍTICA: Ciclo 2: LED2 OFF / LED3 ON
            printf("[T3] CRÍTICA: LED2 OFF / LED3 ON (1000ms)\n");
            led_set_level(LED2_GPIO, 0);
            led_set_level(LED3_GPIO, 1);
            
            // Retraso DENTRO del Mutex (fuerza la retención por 1 segundo)
            vTaskDelay(pdMS_TO_TICKS(1000)); 

            // 2. Liberar el Mutex
            xSemaphoreGive(shared_mutex);
        }
        // Espera corta
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

// -----------------------------------------------------------------
// TAREAS DE MEDICIÓN DE OVERHEAD (Apartado 4) - Prioridad 8
// -----------------------------------------------------------------

// Tarea Auxiliar A: Inicia la medición
void task_a_overhead(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Espera larga para bajo impacto

        ESP_LOGI(TAG, "INICIANDO MEDICIÓN DE OVERHEAD");
        start_cycles = esp_cpu_get_cycle_count();
        vTaskDelay(pdMS_TO_TICKS(1)); // Fuerza el cambio de contexto a Tarea B
    }
}

// Tarea Auxiliar B: Finaliza la medición y calcula el overhead
void task_b_overhead(void *pvParameters) {
    uint32_t end_cycles;
    uint32_t overhead_cycles;
    float overhead_time_us;

    while (1) {
        if (start_cycles != 0) {
            end_cycles = esp_cpu_get_cycle_count();
            
            if (end_cycles >= start_cycles) {
                overhead_cycles = end_cycles - start_cycles;
            } else {
                overhead_cycles = (0xFFFFFFFF - start_cycles) + end_cycles + 1;
            }

            overhead_time_us = (float)overhead_cycles / CPU_FREQ_MHZ;

            // Muestra el resultado en verde usando ESP_LOGI
            ESP_LOGI(TAG, "Resultado: %lu ciclos | %.3f us", overhead_cycles, overhead_time_us);

            start_cycles = 0;
            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }
    }
}

// -----------------------------------------------------------------
// FUNCIÓN PRINCIPAL
// -----------------------------------------------------------------

void app_main() {
    // Crear el Mutex (soporta Priority Inheritance)
    shared_mutex = xSemaphoreCreateMutex();
    
    if (shared_mutex != NULL) {
        // Tarea 1: Intermitencia (Independiente) (Prioridad 5)
        xTaskCreate(led_blink_task_1, "LED1_Blink", 2048, NULL, 5, NULL);
        
        // Tarea 2 y 3: Alternancia y Exclusión Mutua (compiten por el Mutex) (Prioridad 5)
        xTaskCreate(led_control_task_2, "LED_Control_2", 2048, NULL, 5, NULL);
        xTaskCreate(led_control_task_3, "LED_Control_3", 2048, NULL, 5, NULL);

        // Tareas de Medición de Overhead (Prioridad ALTA: 8)
        xTaskCreate(task_a_overhead, "TAREA_A_OVHD", 2048, NULL, 8, NULL);
        xTaskCreate(task_b_overhead, "TAREA_B_OVHD", 2048, NULL, 8, NULL);
    }
}