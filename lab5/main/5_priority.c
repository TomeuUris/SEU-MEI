#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>

// Definición del Mutex que será el recurso compartido
SemaphoreHandle_t mutex;

// PRIORIDADES (de menor a mayor):
// LOW: 3
// MEDIUM: 5
// HIGH: 7

// -----------------------------------------------------
// TAREA DE BAJA PRIORIDAD (LOW: 3)
// -----------------------------------------------------
void task_low(void *pv) {
    int i;
    // La inicialización de 'i' se ha movido dentro del loop para evitar un error de variable sin definir
    // si el compilador no utiliza la variable globalmente.
    
    while (1) {
        printf("[LOW] Intentant agafar el mutex...\n");
        
        // 1. Tarea LOW adquiere el Mutex
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        printf("[LOW] Tinc el mutex! (simulant treball llarg)\n");
        
        // Simula feina larga (bucle corto seguido de un delay)
        i = 0;
        for (i = 0; i < 10; i++) {
            // El trabajo se simula aquí, pero la clave está en el delay y en que la Tarea MEDIUM se ejecute.
            // La instrucción original del for estaba vacía. La dejo vacía para seguir el original.
        }

        // 2. Tarea LOW está en la sección crítica pero llama a vTaskDelay(500)
        vTaskDelay(pdMS_TO_TICKS(500)); // Simula trabajo lento mientras tiene el Mutex
        printf("[LOW] Treball lent... %d/10\n", i + 1); // El i+1 aquí es puramente estético

        // NOTA: El bucle for original tenía un error de alcance de 'i' en el printf de aquí. 
        // Se asume que la intención era mostrar el progreso o simplemente tener un print.
        
        // 3. Tarea LOW libera el Mutex
        printf("[LOW] Alliberant mutex\n");
        xSemaphoreGive(mutex);
        printf("[LOW] Mutex alliberat\n");
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Espera entre ciclos de adquisición
    }
}

// -----------------------------------------------------
// TAREA DE ALTA PRIORIDAD (HIGH: 7)
// -----------------------------------------------------
void task_high(void *pv) {
    while (1) {
        printf(">>>> [HIGH] Intentant agafar el mutex...\n");
        
        // 1. Tarea HIGH intenta adquirir el Mutex (y se bloquea si LOW lo tiene)
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        // 2. Tarea HIGH ha obtenido el Mutex
        printf(">>>> [HIGH] Finalment he obtingut el mutex!\n");
        
        // 3. Tarea HIGH libera el Mutex inmediatamente
        xSemaphoreGive(mutex);
        printf("[HIGH] Mutex alliberat\n");
        
        vTaskDelay(pdMS_TO_TICKS(3000)); // Espera para el próximo ciclo
    }
}

// -----------------------------------------------------
// TAREA DE PRIORIDAD MEDIA (MEDIUM: 5)
// -----------------------------------------------------
void task_medium(void *pv) {
    int i;
    
    while (1) {
        // Esta tarea solo consume CPU y no accede al Mutex.
        // Se ejecutará siempre que LOW (3) o HIGH (7) llamen a vTaskDelay o estén bloqueadas.
        printf("[MEDIUM] Executant-se (interromp LOW!)\n");
        
        i = 0;
        // Simula feina larga que bloquea el recurso
        for (i = 0; i < 3; i++) {
            // bucle vacío, solo consume CPU
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // trabajo lento, cede CPU
        printf("[MEDIUM] Treball lent... %d/3\n", i + 1); 
        printf("[MEDIUM] Treball Finalitzat\n");
        
        vTaskDelay(50); // Pequeña espera entre ciclos de trabajo
    }
}

// -----------------------------------------------------
// FUNCIÓN PRINCIPAL
// -----------------------------------------------------
void app_main() {
    // Inicialización
    mutex = xSemaphoreCreateMutex();

    // 1. Crear Tarea LOW (Prioridad 3)
    xTaskCreate(task_low, "LOW", 4096, NULL, 3, NULL);
    
    // Retardo para asegurar que LOW adquiera el mutex primero (Low Priority Task Acquisition)
    vTaskDelay(pdMS_TO_TICKS(100)); 

    // 2. Crear Tarea HIGH (Prioridad 7)
    xTaskCreate(task_high, "HIGH", 4096, NULL, 7, NULL);
    
    // 3. Crear Tarea MEDIUM (Prioridad 5)
    xTaskCreate(task_medium, "MEDIUM", 4096, NULL, 5, NULL);
}