/*
 * Código de implementación para Lab 4 - Juego A: Gato y Ratón
 * Implementa la Opción A (un solo ESP: ESP-XY), ADAPTADO para recibir COMANDOS CAN.
 * La lógica de movimiento (UP, DOWN, LEFT, RIGHT) se ejecuta en el ESP32.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h> 
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"
#include "driver/ledc.h"
#include "rom/ets_sys.h" // Para ets_delay_us

// --- Configuraciones ---

static const char *TAG = "CAN_GATO_RATON";

// !! IMPORTANTE: AJUSTA ESTOS PINES SEGÚN EL MONTAJE Y EL ESP32-C6 !!
// Pines CAN
#define TX_GPIO_NUM     5 // Pin CAN TX conectado a D(TXD) del transceptor
#define RX_GPIO_NUM     4 // Pin CAN RX conectado a R(RXD) del transceptor

// Pines para la salida PWM al osciloscopio (CH I -> X, CH II -> Y)
#define PWM_X_PIN       23 // Conectar a CH I - X del OSC
#define PWM_Y_PIN       22 // Conectar a CH II - Y del OSC

// Configuración del PWM (Basado en Lab 3)
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_X_CHANNEL      LEDC_CHANNEL_0
#define LEDC_Y_CHANNEL      LEDC_CHANNEL_1

// Parámetros: 250KHz y 8 bits de resolución (0-255)
#define PWM_FREQ            (250000) // Frecuencia de PWM
#define PWM_RESOLUTION      LEDC_TIMER_8_BIT // 8 bits
#define MAX_PWM_VALUE       255      // Valor máximo de coordenada

// --- Protocolo CAN y Lógica del Juego ---
#define PC1_CAN_ID          0x101 // ID para (X1, Y1) - Gato 
#define PC2_CAN_ID          0x102 // ID para (X2, Y2) - Ratón 
#define CATCH_MSG_ID        0x200 // ID para mensaje de "Juego Terminado"
#define CATCH_THRESHOLD     10    // Umbral de colisión (0-255) 
#define MOVEMENT_STEP       10    // Cuánto mover por comando

// Códigos de Comando CAN (data[0])
#define CMD_UP      0
#define CMD_DOWN    1
#define CMD_LEFT    2
#define CMD_RIGHT   3

// --- Parámetros de Visualización (como en el ejemplo del cuadrado) ---
#define CYCLES_PER_POINT    50      // Cuántos ciclos mostrar cada punto antes de alternar
#define DELAY_US_PER_POINT  500     // Microsegundos de delay por punto (ajusta según la persistencia de tu osciloscopio)
#define CAN_CHECK_INTERVAL  100     // Cada cuántos ciclos verificar mensajes CAN (para no ralentizar el dibujo)

// Almacenamiento global (volatile para acceso seguro entre tareas)
volatile int g_x1 = 50,  g_y1 = 50;  // Posición del Gato (PC1)
volatile int g_x2 = 200, g_y2 = 200; // Posición del Ratón (PC2)
volatile bool g_game_over = false;


/**
 * @brief Inicializa los dos canales PWM (X e Y).
 */
static void init_pwm(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = PWM_RESOLUTION, 
        .timer_num        = LEDC_TIMER,
        .freq_hz          = PWM_FREQ,       
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t x_chan_conf = {
        .gpio_num   = PWM_X_PIN, .speed_mode = LEDC_MODE, .channel = LEDC_X_CHANNEL,
        .timer_sel  = LEDC_TIMER, .duty = 0, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&x_chan_conf));

    ledc_channel_config_t y_chan_conf = {
        .gpio_num   = PWM_Y_PIN, .speed_mode = LEDC_MODE, .channel = LEDC_Y_CHANNEL,
        .timer_sel  = LEDC_TIMER, .duty = 0, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&y_chan_conf));
    
    ESP_LOGI(TAG, "Canales PWM (X, Y) inicializados a %d Hz, 8 bits.", PWM_FREQ);
}

/**
 * @brief Actualiza los ciclos de trabajo de PWM para X e Y.
 */
static void set_pwm_coords(uint8_t x, uint8_t y)
{
    ledc_set_duty(LEDC_MODE, LEDC_X_CHANNEL, x);
    ledc_update_duty(LEDC_MODE, LEDC_X_CHANNEL);
    
    ledc_set_duty(LEDC_MODE, LEDC_Y_CHANNEL, y);
    ledc_update_duty(LEDC_MODE, LEDC_Y_CHANNEL);
}

/**
 * @brief Aplica un comando de movimiento a las coordenadas de un jugador, asegurando que se mantengan entre 0 y 255.
 */
static void apply_command(uint32_t player_id, uint8_t command)
{
    volatile int *x_ptr, *y_ptr;

    if (player_id == PC1_CAN_ID) {
        x_ptr = &g_x1;
        y_ptr = &g_y1;
    } else if (player_id == PC2_CAN_ID) {
        x_ptr = &g_x2;
        y_ptr = &g_y2;
    } else {
        return; // ID no reconocido
    }

    switch (command) {
        case CMD_UP:
            *y_ptr += MOVEMENT_STEP;
            break;
        case CMD_DOWN:
            *y_ptr -= MOVEMENT_STEP;
            break;
        case CMD_LEFT:
            *x_ptr -= MOVEMENT_STEP;
            break;
        case CMD_RIGHT:
            *x_ptr += MOVEMENT_STEP;
            break;
        default:
            return; // Comando no reconocido
    }
    
    // Asegurar que las coordenadas estén dentro del rango [0, 255]
    if (*x_ptr < 0) *x_ptr = 0;
    if (*x_ptr > MAX_PWM_VALUE) *x_ptr = MAX_PWM_VALUE;
    if (*y_ptr < 0) *y_ptr = 0;
    if (*y_ptr > MAX_PWM_VALUE) *y_ptr = MAX_PWM_VALUE;
}


/**
 * @brief Tarea principal del juego (Recepción CAN, Visualización y Lógica).
 * Usa delays en MICROSEGUNDOS para multiplexación rápida (como en el ejemplo del cuadrado).
 */
static void game_task(void *arg)
{
    ESP_LOGI(TAG, "Tarea del juego iniciada.");
    bool show_point_1 = true;
    twai_message_t rx_message;
    
    int display_counter = 0;
    int can_check_counter = 0;

    while (!g_game_over) {
        
        // 1. Verificar mensajes CAN periódicamente (no en cada ciclo para no ralentizar)
        can_check_counter++;
        if (can_check_counter >= CAN_CHECK_INTERVAL) {
            can_check_counter = 0;
            
            esp_err_t ret = twai_receive(&rx_message, 0); // Timeout 0 = no bloqueante

            if (ret == ESP_OK) {
                if ((rx_message.identifier == PC1_CAN_ID || rx_message.identifier == PC2_CAN_ID) && 
                    rx_message.data_length_code >= 1) {
                    
                    apply_command(rx_message.identifier, rx_message.data[0]);
                    
                    ESP_LOGI(TAG, "ID %s cmd %d. Pos: G(%d,%d) R(%d,%d)", 
                             rx_message.identifier == PC1_CAN_ID ? "0x101" : "0x102", 
                             rx_message.data[0], g_x1, g_y1, g_x2, g_y2);
                }
            } else if (ret != ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "Fallo al recibir: %s", esp_err_to_name(ret));
            }
            
            // 3. Comprobar colisión (solo cuando verificamos CAN)
            int dx = abs(g_x1 - g_x2);
            int dy = abs(g_y1 - g_y2);

            if (dx < CATCH_THRESHOLD && dy < CATCH_THRESHOLD) {
                g_game_over = true;
                ESP_LOGI(TAG, "¡GATO ATRAPA RATÓN! G(%d,%d) R(%d,%d)", g_x1, g_y1, g_x2, g_y2);
                
                twai_message_t catch_msg = {0};
                catch_msg.identifier = CATCH_MSG_ID;
                catch_msg.data_length_code = 1;
                catch_msg.data[0] = 0x01;
                
                twai_transmit(&catch_msg, portMAX_DELAY);
                ESP_LOGI(TAG, "Mensaje 'Game Over' enviado.");
            }
        }
        
        // 2. Actualizar el osciloscopio (Multiplexación RÁPIDA con microsegundos)
        if (show_point_1) {
            set_pwm_coords((uint8_t)g_x1, (uint8_t)g_y1); // Mostrar Gato
        } else {
            set_pwm_coords((uint8_t)g_x2, (uint8_t)g_y2); // Mostrar Ratón
        }
        
        // DELAY EN MICROSEGUNDOS (clave para que funcione correctamente)
        ets_delay_us(DELAY_US_PER_POINT);
        
        // Alternar punto después de N ciclos
        display_counter++;
        if (display_counter >= CYCLES_PER_POINT) {
            show_point_1 = !show_point_1;
            display_counter = 0;
        }
    } 

    ESP_LOGI(TAG, "Juego terminado. Finalizando tarea.");
    set_pwm_coords(0, 0); 
    vTaskDelete(NULL);
}

void app_main(void)
{
    init_pwm();

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
    const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_LOGI(TAG, "Instalando driver TWAI...");
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "Driver instalado.");

    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "Driver iniciado.");

    xTaskCreate(game_task, "game_task", 4096, NULL, 10, NULL);
}