#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <stdio.h>
#include "esp_cpu.h"

// --- Configuració de la CPU i Mesura ---
#define CPU_FREQ_MHZ 160
#define NUM_MEASUREMENTS_TOTAL 1000 // Total de mesures a fer abans d'aturar-se
#define REPORT_FREQUENCY 3         // Freqüència d'impressió: mostrar la mitjana cada 10 mesures

#define GET_CYCLE_COUNT() esp_cpu_get_cycle_count()

// --- Definició de GPIO/LEDs ---
#define LED1_GPIO 1
#define LED2_GPIO 2
#define LED3_GPIO 3

// --- Variables Globals ---
SemaphoreHandle_t shared_mutex;

// Variables per la mesura d'Overhead del Mutex
static volatile uint32_t T_start_give = 0;
static volatile uint64_t Total_Mutex_Overhead_Cycles = 0;
static volatile uint32_t Measurements_Total = 0; // Comptador total global
static volatile bool measurement_done = false;

// Variables per al càlcul de la mitjana intermèdia
static uint64_t report_cycle_sum = 0;
static uint32_t report_count = 0;

// --------------------------------------------------------

// --- FUNCIONS DE HARDWARE ---
void led_init(int gpio) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE, .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << gpio), .pull_down_en = 0, .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio, 0); 
}

void led_set_level(int gpio, int level) {
    gpio_set_level(gpio, level);
}

// --------------------------------------------------------

// --- TASCA 1: LED Independent ---
void led_blink_task_1(void *pvParameters) {
    led_init(LED1_GPIO); 
    while (1) {
        led_set_level(LED1_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        led_set_level(LED1_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// --------------------------------------------------------

// --- TASQUES 2 & 3: Alternança amb Exclusió Mútua i Mesura d'Overhead ---

void LED2_Control_Task(void *pvParameters) {
    printf("Configurant LED2 (Control)...\n");
    led_init(LED2_GPIO); 
    uint32_t T_end_take;
    uint32_t current_overhead;
    
    // Tasca per a LED2, també encarregada de calcular i imprimir la mitjana.
    while (!measurement_done) {
        
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            // 1. Punt de Mesura (Final del Context Switch)
            T_end_take = GET_CYCLE_COUNT();
            
            // 2. Càlcul de l'Overhead (Give -> Context Switch -> Take)
            if (T_start_give != 0 && T_end_take > T_start_give) {
                current_overhead = T_end_take - T_start_give;
                
                // Comptadors globals per la mitjana total
                Total_Mutex_Overhead_Cycles += current_overhead;
                Measurements_Total++;

                // Comptadors pel report intermig
                report_cycle_sum += current_overhead;
                report_count++;

                // --- CÀLCUL I IMPRESSIÓ CONTINUA (cada REPORT_FREQUENCY) ---
                if (report_count >= REPORT_FREQUENCY) {
                    float avg_cycles = (float)report_cycle_sum / (float)report_count;
                    float overhead_us = avg_cycles / CPU_FREQ_MHZ; 
                    
                    printf("\n[Mesura %lu-%lu] Overhead Mitjà: %.2f cicles (%.3f us)\n", 
                           (unsigned3  long)(Measurements_Total - report_count + 1),
                           (unsigned3  long)Measurements_Total,
                           avg_cycles, overhead_us);

                    // Reiniciar els comptadors de report
                    report_cycle_sum = 0;
                    report_count = 0;

                    // Si hem acabat totes les mesures totals, senyalitzem la finalització
                    if (Measurements_Total >= NUM_MEASUREMENTS_TOTAL) {
                        measurement_done = true;
                    }
                }
            }
            
            // 3. Lògica LED: LED2 ON, LED3 OFF
            led_set_level(LED2_GPIO, 1);
            led_set_level(LED3_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // 4. Punt de Mesura (Inici del Context Switch)
            T_start_give = GET_CYCLE_COUNT();

            // 5. Alliberar i cedir el control
            xSemaphoreGive(shared_mutex);
            taskYIELD();
        }
    }
    
    // --- Impressió del resultat FINAL (opcional) ---
    if (Measurements_Total > 0) {
        float avg_cycles_final = (float)Total_Mutex_Overhead_Cycles / (float)Measurements_Total;
        float overhead_us_final = avg_cycles_final / CPU_FREQ_MHZ; 
        
        printf("\n============================================\n");
        printf("<<< RESULTAT FINAL OVERHEAD MUTEX >>>\n");
        printf("Total de mesures: %lu\n", (unsigned long)Measurements_Total);
        printf("Mitjana FINAL: %.2f cicles (%.3f us)\n", avg_cycles_final, overhead_us_final);
        printf("============================================\n");
    }
    
    led_set_level(LED2_GPIO, 0);
    led_set_level(LED3_GPIO, 0);
    vTaskDelete(NULL);
}


void LED3_Control_Task(void *pvParameters) {
    printf("Configurant LED3...\n");
    led_init(LED3_GPIO);
    
    // Tasca per a LED3
    while (!measurement_done) {
        
        if (xSemaphoreTake(shared_mutex, portMAX_DELAY) == pdPASS) {
            
            // 1. Lògica LED: LED3 ON, LED2 OFF
            led_set_level(LED3_GPIO, 1);
            led_set_level(LED2_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // 2. Punt de Mesura (Inici del Context Switch)
            T_start_give = GET_CYCLE_COUNT();

            // 3. Alliberar i cedir el control
            xSemaphoreGive(shared_mutex); 
            taskYIELD();
        }
    }
    
    // Assegurar que LED2_Control_Task acabi la impressió final
    vTaskDelay(pdMS_TO_TICKS(100)); 
    vTaskDelete(NULL); 
}


// --- FUNCIÓ PRINCIPAL app_main ---

void app_main() {
    printf("Iniciant aplicació de LEDs i mesurament continu d'overhead (Report cada %d)...\n", REPORT_FREQUENCY);

    shared_mutex = xSemaphoreCreateMutex();
    
    if (shared_mutex != NULL) {
        const UBaseType_t UX_ALTERNATE_PRIORITY = 5; 
        const UBaseType_t UX_BLINK_PRIORITY = 3; 

        xTaskCreate(led_blink_task_1, "LED1_Blink", 2048, NULL, UX_BLINK_PRIORITY, NULL);
        xTaskCreate(LED2_Control_Task, "LED2_Control", 4096, NULL, UX_ALTERNATE_PRIORITY, NULL);
        xTaskCreate(LED3_Control_Task, "LED3_Control", 4096, NULL, UX_ALTERNATE_PRIORITY, NULL);
        
        printf("Tasques creades. L'alternança s'aturarà després de %d mesures.\n", NUM_MEASUREMENTS_TOTAL);

    } else {
         printf("ERROR: Fallo al crear el Mutex.\n");
    }
}