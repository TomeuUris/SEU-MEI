#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h" // NECESARIO para esp_timer_get_time()
#include <stdio.h>

// Define CONFIG_FREERTOS_HZ if not defined
#ifndef CONFIG_FREERTOS_HZ
#define CONFIG_FREERTOS_HZ 1000
#endif

// Definiciones de GPIO/LEDs (Tarea 2)
#define LED1_GPIO 1
#define LED2_GPIO 2
#define LED3_GPIO 3

// Variables de sincronización (Tarea 2)
SemaphoreHandle_t shared_mutex;

// --- Variables para la medición del Overhead (Tarea 4) ---
static int64_t task_switch_out_time = 0;
static int64_t task_switch_in_time = 0;
// --------------------------------------------------------

// --- FUNCIONES DE HARDWARE (TAREA 2) ---

void led_init(int gpio) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << gpio),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio, 0); 
    // printf("LED GPIO %d initialized\n", gpio); // Se comenta para reducir ruido
}

void led_set_level(int gpio, int level) {
    gpio_set_level(gpio, level);
    // printf("LED GPIO %d set to level %d\n", gpio, level); // Se comenta para reducir ruido
}

// --- TAREAS DE LED (TAREA 2) ---

void led_blink_task_1(void *pvParameters) {
    led_init(LED1_GPIO); 
    while (1) {
        // vTaskDelay() provoca un cambio de contexto (y mide T_inicio)
        led_set_level(LED1_GPIO, 1); // ON
        vTaskDelay(pdMS_TO_TICKS(300));
        
        // vTaskDelay() provoca un cambio de contexto (y mide T_inicio)
        led_set_level(LED1_GPIO, 0); // OFF
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void led_alternate_task_2_3_mutex(void *pvParameters) {
    led_init(LED2_GPIO); 
    led_init(LED3_GPIO);
    
    while (1) {
        // xSemaphoreTake() puede causar bloqueo/cambio de contexto (mide T_inicio)
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            printf("[Mutex] LED2 ON / LED3 OFF\n");
            led_set_level(LED2_GPIO, 1);
            led_set_level(LED3_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1000)); // Causa cambio de contexto
            
            printf("[Mutex] LED2 OFF / LED3 ON\n");
            led_set_level(LED2_GPIO, 0);
            led_set_level(LED3_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(1000)); // Causa cambio de contexto
            
            // xSemaphoreGive() puede causar cambio de contexto
            xSemaphoreGive(shared_mutex);
        }
    }
}

// --- FUNCIONES DE RASTREO (HOOKS DEL SCHEDULER) (TAREA 4) ---

/**
 * @brief Hook llamado ANTES de que el kernel cambie de contexto. Mide T_inicio.
 */
void vApplicationTaskSwitchOut(void) {
    // Capturar el tiempo en microsegundos al salir de la tarea actual
    task_switch_out_time = esp_timer_get_time();
}

/**
 * @brief Hook llamado DESPUÉS de que el kernel ha entrado en la nueva tarea. Mide T_fin y calcula Overhead.
 */
void vApplicationTaskSwitchIn(void) {
    // Capturar el tiempo en microsegundos al entrar a la nueva tarea
    task_switch_in_time = esp_timer_get_time();
    
    // Calcular el overhead: T_fin - T_inicio
    int64_t overhead = task_switch_in_time - task_switch_out_time;
    
    // Imprimir el resultado (solo si es un cambio válido)
    if (overhead > 0) {
        printf("\n<<< MEDICIÓN DE OVERHEAD >>>\n");
        printf("Task Entrante: %s\n", pcTaskGetName(NULL));
        // El overhead incluye el tiempo dedicado a las instrucciones de la tarea de origen que provocan el salto de tarea, 
        // más la interrupción del kernel (PendSV), más el tiempo que el scheduler tarda en decidir qué tarea es la siguiente en ejecutar, 
        // más el cambio de contexto[cite: 22].
        printf("Tiempo de Overhead del Scheduler: %lld µs\n", overhead); 
        printf("<<< FIN MEDICIÓN >>>\n\n");
    }
}


// --- FUNCIÓN PRINCIPAL app_main ---

void app_main() {
    // Crear el Mutex (soporta Priority Inheritance)
    shared_mutex = xSemaphoreCreateMutex();
    
    if (shared_mutex != NULL) {
        // Tarea 1: Intermitencia (Independiente)
        xTaskCreate(led_blink_task_1, "LED1_Blink", 2048, NULL, 5, NULL);
        
        // Tarea 2/3: Alternancia (Protegida por Mutex)
        xTaskCreate(led_alternate_task_2_3_mutex, "LED23_Alternate", 2048, NULL, 5, NULL);
        
        printf("Sistema de LEDs (Tarea 2) instrumentado (Tarea 4) y listo para medir el Overhead.\n");
    } else {
         printf("ERROR: Fallo al crear el Mutex.\n");
    }
}