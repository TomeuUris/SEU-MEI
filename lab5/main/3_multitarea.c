#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <stdio.h>

// --- I. DEFINICIONES GENERALES ---

#ifndef CONFIG_FREERTOS_HZ
#define CONFIG_FREERTOS_HZ 1000
#endif

// Definiciones de GPIO/LEDs (Tarea 2)
#define LED1_GPIO 1
#define LED2_GPIO 2
#define LED3_GPIO 3

// Parámetros de la Tarea 3 (P/C)
#define QUEUE_LENGTH 16
#define PRODUCER_PRIORITY (tskIDLE_PRIORITY + 2) // Prioridad 5 (Mayor)
#define CONSUMER_PRIORITY (tskIDLE_PRIORITY + 1) // Prioridad 4 (Menor)
#define CONSUMER_DELAY_MS 200 

// --- II. VARIABLES Y HANDLES ---

// Tarea 3 (P/C)
QueueHandle_t number_queue;
SemaphoreHandle_t p1_turn_semaphore; // Sincronización P/C
SemaphoreHandle_t p2_turn_semaphore; // Sincronización P/C
static int next_even = 0;
static int next_odd = 1;

// Tarea 2 (LEDs)
SemaphoreHandle_t shared_mutex; // Exclusión mutua LED2/LED3


// --- III. FUNCIONES DE HARDWARE (TAREA 2) ---

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
    printf("LED GPIO %d initialized\n", gpio);
}

void led_set_level(int gpio, int level) {
    gpio_set_level(gpio, level);
    // Nota: Se elimina el printf aquí para reducir el ruido en el log
    // y no interferir con la salida P/C y la medición del overhead (Tarea 4).
}


// --- IV. TAREAS DE LED (TAREA 2) ---

/**
 * @brief Tarea LED1: Intermitencia de 0.3 segundos (Independiente).
 */
void led_blink_task_1(void *pvParameters) {
    led_init(LED1_GPIO); 
    while (1) {
        led_set_level(LED1_GPIO, 1); // ON
        vTaskDelay(pdMS_TO_TICKS(300));
        led_set_level(LED1_GPIO, 0); // OFF
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/**
 * @brief Tarea LED2/LED3: Alternancia de 1 segundo (Protegida por Mutex).
 */
void led_alternate_task_2_3_mutex(void *pvParameters) {
    led_init(LED2_GPIO); 
    led_init(LED3_GPIO);
    
    while (1) {
        // Adquirir el Mutex
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            // Sección Crítica
            printf("[LED Mutex] Alternando: LED2 ON / LED3 OFF\n");
            led_set_level(LED2_GPIO, 1);
            led_set_level(LED3_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));

            printf("[LED Mutex] Alternando: LED2 OFF / LED3 ON\n");
            led_set_level(LED2_GPIO, 0);
            led_set_level(LED3_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // Liberar el Mutex
            xSemaphoreGive(shared_mutex);
        }
    }
}

// --- V. TAREAS PRODUCTORAS Y CONSUMIDORAS (TAREA 3) ---

void producer_task_even(void *pvParameters) {
    while (1) {
        xSemaphoreTake(p1_turn_semaphore, portMAX_DELAY); // Esperar turno

        printf("[P1] Produciendo par: %d\n", next_even);
        
        if (xQueueSend(number_queue, &next_even, portMAX_DELAY) == pdPASS) {
            next_even += 2;
        } 
        
        xSemaphoreGive(p2_turn_semaphore); // Ceder turno a P2
    }
}

void producer_task_odd(void *pvParameters) {
    while (1) {
        xSemaphoreTake(p2_turn_semaphore, portMAX_DELAY); // Esperar turno

        printf("[P2] Produciendo impar: %d\n", next_odd);
        
        if (xQueueSend(number_queue, &next_odd, portMAX_DELAY) == pdPASS) {
            next_odd += 2;
        } 
        
        xSemaphoreGive(p1_turn_semaphore); // Ceder turno a P1
    }
}

void consumer_task(void *pvParameters) {
    int consumed_value;
    int target_parity = (int)pvParameters;
    char *task_name = pcTaskGetName(NULL);

    while (1) {
        if (xQueueReceive(number_queue, &consumed_value, portMAX_DELAY) == pdPASS) {
            
            int current_parity = consumed_value % 2;

            if (current_parity == target_parity) {
                printf("[%s] CONSUMIDO: %d (Mi objetivo: %s)\n", 
                       task_name, consumed_value, (target_parity == 0 ? "Par" : "Impar"));
            } else {
                if (xQueueSendToFront(number_queue, &consumed_value, (TickType_t)0) != pdPASS) {
                    // Manejo de error si la cola está llena al devolver el mensaje
                }
                
                printf("[%s] Devuelto: %d (No es mi objetivo), cediendo CPU.\n", task_name, consumed_value);
                taskYIELD(); 
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONSUMER_DELAY_MS));
    }
}


// --- VI. FUNCIÓN PRINCIPAL app_main ---

void app_main() {
    // 1. Inicialización de Semáforos y Cola (Tarea 3)
    p1_turn_semaphore = xSemaphoreCreateBinary();
    p2_turn_semaphore = xSemaphoreCreateBinary();
    number_queue = xQueueCreate(QUEUE_LENGTH, sizeof(int));
    
    // 2. Inicialización de Mutex (Tarea 2)
    shared_mutex = xSemaphoreCreateMutex(); 

    if (number_queue == NULL || p1_turn_semaphore == NULL || p2_turn_semaphore == NULL || shared_mutex == NULL) {
        printf("ERROR: Fallo al inicializar uno de los componentes de FreeRTOS.\n");
        return;
    }
    
    // 3. Crear Tareas de LEDs (Tarea 2)
    // Se les puede asignar la misma prioridad 5 que a los productores para que compitan
    xTaskCreate(led_blink_task_1, "LED1_Blink", 2048, NULL, 5, NULL);
    xTaskCreate(led_alternate_task_2_3_mutex, "LED23_Alternate", 2048, NULL, 5, NULL);

    // 4. Crear Tareas P/C (Tarea 3)
    xTaskCreate(producer_task_even, "P1_Par", 4096, NULL, PRODUCER_PRIORITY, NULL);
    xTaskCreate(producer_task_odd, "P2_Impar", 4096, NULL, PRODUCER_PRIORITY, NULL);
    xTaskCreate(consumer_task, "C1_Impar", 4096, (void *)1, CONSUMER_PRIORITY, NULL);
    xTaskCreate(consumer_task, "C3_Impar", 4096, (void *)1, CONSUMER_PRIORITY, NULL);
    xTaskCreate(consumer_task, "C2_Par", 4096, (void *)0, CONSUMER_PRIORITY, NULL);
    xTaskCreate(consumer_task, "C4_Par", 4096, (void *)0, CONSUMER_PRIORITY, NULL);
    
    // 5. Iniciar sincronización P/C
    xSemaphoreGive(p1_turn_semaphore); 
    
    printf("Sistema de 8 tareas (P/C y LEDs) inicializado y corriendo.\n");
}