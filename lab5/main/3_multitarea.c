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

// --- II. VARIABLES Y HANDLES ---

// Tarea 3 (P/C)
QueueHandle_t even_number_queue; // Cola para pares (C2, C4)
QueueHandle_t odd_number_queue;  // Cola para impares (C1, C3)
SemaphoreHandle_t p1_turn_semaphore;     // Sincronización de turno P1 (Pares)
SemaphoreHandle_t p2_turn_semaphore;     // Sincronización de turno P2 (Impares)
SemaphoreHandle_t even_consumer_semaphore; // Semáforo de Conteo para equidad C2/C4
SemaphoreHandle_t odd_consumer_semaphore;  // Semáforo de Conteo para equidad C1/C3

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
    gpio_set_level(gpio, 0); // Iniciar en OFF
    printf("LED GPIO %d initialized\n", gpio);
}

void led_set_level(int gpio, int level) {
    gpio_set_level(gpio, level);
    printf("LED GPIO %d set to level %d\n", gpio, level);
}


// --- IV. TAREAS DE LED (TAREA 2) - SEPARADAS ---

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
 * @brief Tarea LED2: Controla el Ciclo 1 (LED2 ON), compite por Mutex.
 */
void led_control_task_2_mutex(void *pvParameters) {
    led_init(LED2_GPIO); 
    
    while (1) {
        // 1. Intentar adquirir el Mutex
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            // Sección Crítica: Ciclo 1 (1000 ms)
            printf("[Mutex-T2] LED2 ON / LED3 OFF\n");
            led_set_level(LED2_GPIO, 1);
            led_set_level(LED3_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // 2. Liberar el Mutex
            xSemaphoreGive(shared_mutex);
        }
        // Espera para permitir la competencia
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Tarea LED3: Controla el Ciclo 2 (LED3 ON), compite por Mutex.
 */
void led_control_task_3_mutex(void *pvParameters) {
    led_init(LED3_GPIO); 
    
    while (1) {
        // 1. Intentar adquirir el Mutex
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            // Sección Crítica: Ciclo 2 (1000 ms)
            printf("[Mutex-T3] LED2 OFF / LED3 ON\n");
            led_set_level(LED2_GPIO, 0);
            led_set_level(LED3_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // 2. Liberar el Mutex
            xSemaphoreGive(shared_mutex);
        }
        // Espera para permitir la competencia
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


// --- V. TAREAS PRODUCTORAS Y CONSUMIDORAS (TAREA 3) - CORREGIDAS ---

void producer_task_even(void *pvParameters) {
    while (1) {
        xSemaphoreTake(p1_turn_semaphore, portMAX_DELAY); // Esperar turno

        printf("[P1] Produciendo par: %d\n", next_even);
        
        // Enviar a la cola de pares
        if (xQueueSend(even_number_queue, &next_even, portMAX_DELAY) == pdPASS) {
            next_even += 2;
        } 
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Ceder turno a P2 y dar token de consumo a un consumidor PAR
        xSemaphoreGive(p2_turn_semaphore); 
        xSemaphoreGive(even_consumer_semaphore);
    }
}

void producer_task_odd(void *pvParameters) {
    while (1) {
        xSemaphoreTake(p2_turn_semaphore, portMAX_DELAY); // Esperar turno

        printf("[P2] Produciendo impar: %d\n", next_odd);
        
        // Enviar a la cola de impares
        if (xQueueSend(odd_number_queue, &next_odd, portMAX_DELAY) == pdPASS) {
            next_odd += 2;
        } 
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Ceder turno a P1 y dar token de consumo a un consumidor IMPAR
        xSemaphoreGive(p1_turn_semaphore);
        xSemaphoreGive(odd_consumer_semaphore);
    }
}

void consumer_task(void *pvParameters) {
    int target_parity = (int)pvParameters; 
    QueueHandle_t target_queue = (target_parity == 0) ? even_number_queue : odd_number_queue;
    SemaphoreHandle_t consumer_semaphore = (target_parity == 0) ? even_consumer_semaphore : odd_consumer_semaphore;
    char *task_name = pcTaskGetName(NULL);
    int consumed_value;
    
    while (1) {
        // 1. Esperar el turno de consumo (garantiza equidad)
        xSemaphoreTake(consumer_semaphore, portMAX_DELAY);

        // 2. Recibir de la cola (sabemos que el mensaje es de la paridad correcta)
        if (xQueueReceive(target_queue, &consumed_value, portMAX_DELAY) == pdPASS) {
            printf("[%s] CONSUMIDO: %d (Mi objetivo: %s)\n", 
                   task_name, consumed_value, (target_parity == 0 ? "Par" : "Impar"));
        }
    }
}


// --- VI. FUNCIÓN PRINCIPAL app_main ---

void app_main() {
    // 1. Inicialización de Semáforos y Colas (Tarea 3)
    p1_turn_semaphore = xSemaphoreCreateBinary();
    p2_turn_semaphore = xSemaphoreCreateBinary();
    even_number_queue = xQueueCreate(QUEUE_LENGTH, sizeof(int)); 
    odd_number_queue = xQueueCreate(QUEUE_LENGTH, sizeof(int));  
    
    // Semáforos de conteo para equidad: Capacidad 2, inicial 0.
    even_consumer_semaphore = xSemaphoreCreateCounting(2, 0); 
    odd_consumer_semaphore = xSemaphoreCreateCounting(2, 0); 
    
    // 2. Inicialización de Mutex (Tarea 2)
    shared_mutex = xSemaphoreCreateMutex(); 

    if (even_number_queue == NULL || odd_number_queue == NULL || shared_mutex == NULL) {
        printf("ERROR: Fallo al inicializar uno de los componentes de FreeRTOS.\n");
        return;
    }
    
    // 3. Crear Tareas de LEDs (Tarea 2) - AHORA SEPARADAS Y COMPITIENDO
    /*xTaskCreate(led_blink_task_1, "LED1_Blink", 2048, NULL, 5, NULL);
    xTaskCreate(led_control_task_2_mutex, "LED_Control_2", 2048, NULL, 5, NULL);
    xTaskCreate(led_control_task_3_mutex, "LED_Control_3", 2048, NULL, 5, NULL);*/

    // 4. Crear Tareas P/C (Tarea 3)
    xTaskCreate(producer_task_even, "P1_Par", 4096, NULL, PRODUCER_PRIORITY, NULL);
    xTaskCreate(producer_task_odd, "P2_Impar", 4096, NULL, PRODUCER_PRIORITY, NULL);
    
    // Consumidores Impares (C1, C3)
    xTaskCreate(consumer_task, "C1_Impar", 4096, (void *)1, CONSUMER_PRIORITY, NULL);
    xTaskCreate(consumer_task, "C3_Impar", 4096, (void *)1, CONSUMER_PRIORITY, NULL);
    
    // Consumidores Pares (C2, C4)
    xTaskCreate(consumer_task, "C2_Par", 4096, (void *)0, CONSUMER_PRIORITY, NULL);
    xTaskCreate(consumer_task, "C4_Par", 4096, (void *)0, CONSUMER_PRIORITY, NULL);
    
    // 5. Iniciar sincronización P/C
    xSemaphoreGive(p1_turn_semaphore); 
    
    printf("Sistema de 8 tareas (P/C y LEDs) inicializado y corriendo.\n");
}