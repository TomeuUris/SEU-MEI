/*
 * Código de implementación para Lab 4 - Juego A: Gato y Ratón
 * [cite_start]Implementa la Opción B (un solo ESP)[cite: 102].
 *
 * Este código combina:
 * [cite_start]1. La lógica del bus CAN (Lab 4) [cite: 88] para recibir coordenadas.
 * [cite_start]2. La generación de PWM de alta frecuencia (Lab 3) [cite: 125, 133]
 * [cite_start]para controlar el osciloscopio en modo X-Y[cite: 136].
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h> // Para abs()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
[cite_start]#include "driver/twai.h"    // API para el bus CAN (Lab 4) [cite: 21]
[cite_start]#include "driver/ledc.h"    // API para PWM (Lab 3) [cite: 129]

// --- Configuraciones ---

static const char *TAG = "CAN_GATO_RATON";

// !! IMPORTANTE: AJUSTA ESTOS PINES !!
[cite_start]// Pines CAN (Lab 4) [cite: 36]
#define TX_GPIO_NUM     5 // Pin CAN TX conectado a D(TXD) del transceptor
#define RX_GPIO_NUM     4 // Pin CAN RX conectado a R(RXD) del transceptor

[cite_start]// Pines para la salida PWM al osciloscopio (Lab 3) [cite: 138]
#define PWM_X_PIN       23 // Conectar a CH I - X del OSC (con filtro RC)
#define PWM_Y_PIN       22 // Conectar a CH II - Y del OSC (con filtro RC)

// Configuración del PWM (Basado en Lab 3)
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_X_CHANNEL      LEDC_CHANNEL_0
#define LEDC_Y_CHANNEL      LEDC_CHANNEL_1

// Parámetros de Lab 3: 250KHz y 8 bits de resolución
[cite_start]#define PWM_FREQ            (250000) //  250 KHz, ideal para filtro RC [cite: 160]
#define PWM_RESOLUTION      LEDC_TIMER_8_BIT //  8 bits (0-255)

// --- Protocolo CAN y Lógica del Juego (Lab 4) ---
[cite_start]#define PC1_CAN_ID          0x101 // ID para mensajes de (X1, Y1) [cite: 10]
[cite_start]#define PC2_CAN_ID          0x102 // ID para mensajes de (X2, Y2) [cite: 11]
#define CATCH_MSG_ID        0x200 // ID para mensaje de "Juego Terminado"
[cite_start]#define CATCH_THRESHOLD     10    // Umbral de colisión (0-255) [cite: 12]

// Almacenamiento global (volatile para acceso seguro entre tareas)
// ---!!! LÍNEAS ACTUALIZADAS !!!---
volatile uint8_t g_x1 = 50,  g_y1 = 50;  // Posición Gato (inicia esq. inf-izq)
volatile uint8_t g_x2 = 200, g_y2 = 200; // Posición Ratón (inicia esq. sup-der)
volatile bool g_game_over = false;
// ---!!! -------------------- !!!---


/**
 * @brief Inicializa los dos canales PWM (X e Y)
 * (Lógica de Lab 3)
 */
static void init_pwm(void)
{
    // 1. Configurar el temporizador PWM
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = PWM_RESOLUTION, // 8 bits 
        .timer_num        = LEDC_TIMER,
        .freq_hz          = PWM_FREQ,       // 250 KHz 
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // 2. Configurar canal PWM para X
    ledc_channel_config_t x_chan_conf = {
        .gpio_num   = PWM_X_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_X_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&x_chan_conf));

    // 3. Configurar canal PWM para Y
    ledc_channel_config_t y_chan_conf = {
        .gpio_num   = PWM_Y_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_Y_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&y_chan_conf));
    
    ESP_LOGI(TAG, "Canales PWM (X, Y) inicializados a %d Hz, 8 bits.", PWM_FREQ);
}

/**
 * @brief Actualiza los ciclos de trabajo de PWM para X e Y
 */
static void set_pwm_coords(uint8_t x, uint8_t y)
{
    // Escribir el valor 0-255 directamente como ciclo de trabajo (duty)
    ledc_set_duty(LEDC_MODE, LEDC_X_CHANNEL, x);
    ledc_update_duty(LEDC_MODE, LEDC_X_CHANNEL);
    
    ledc_set_duty(LEDC_MODE, LEDC_Y_CHANNEL, y);
    ledc_update_duty(LEDC_MODE, LEDC_Y_CHANNEL);
}

/**
 * @brief Tarea principal del juego.
 *
 * 1. Escucha mensajes CAN de forma no bloqueante.
 * 2. Actualiza las coordenadas globales (g_x1, g_y1, g_x2, g_y2).
 * 3. Alterna (multiplexa) la salida PWM para mostrar ambos puntos.
 * 4. Comprueba la condición de victoria (colisión).
 */
static void game_task(void *arg)
{
    ESP_LOGI(TAG, "Tarea del juego iniciada.");
    bool show_point_1 = true;
    twai_message_t rx_message;

    while (!g_game_over) {
        
        // 1. Recibir mensajes CAN (con un timeout corto para no bloquear el bucle)
        [cite_start]// El PC debe enviar datos periódicamente (ej. 40ms) [cite: 88]
        esp_err_t ret = twai_receive(&rx_message, pdMS_TO_TICKS(5)); 

        if (ret == ESP_OK) {
            // Mensaje recibido. Comprobar ID.
            if (rx_message.identifier == PC1_CAN_ID && rx_message.data_length_code >= 2) {
                g_x1 = rx_message.data[0]; // X1
                g_y1 = rx_message.data[1]; // Y1
            } else if (rx_message.identifier == PC2_CAN_ID && rx_message.data_length_code >= 2) {
                g_x2 = rx_message.data[0]; // X2
                g_y2 = rx_message.data[1]; // Y2
            }
        } else if (ret != ESP_ERR_TIMEOUT) {
            // Error real (no un simple timeout)
            ESP_LOGE(TAG, "Fallo al recibir mensaje: %s", esp_err_to_name(ret));
        }
        
        // 2. Actualizar el osciloscopio (Multiplexación de puntos)
        // Se alterna entre mostrar el punto 1 y el punto 2 muy rápido.
        // El timeout de 5ms en twai_receive() + el código aquí
        // crea un refresco rápido para que se vean los dos puntos.
        if (show_point_1) {
            set_pwm_coords(g_x1, g_y1); // Mostrar Gato
        } else {
            set_pwm_coords(g_x2, g_y2); // Mostrar Ratón
        }
        show_point_1 = !show_point_1; // Alternar para el próximo ciclo

        [cite_start]// 3. Comprobar la condición de "catch" [cite: 12]
        int dx = abs((int)g_x1 - (int)g_x2);
        int dy = abs((int)g_y1 - (int)g_y2);

        if (dx < CATCH_THRESHOLD && dy < CATCH_THRESHOLD) {
            g_game_over = true;
            ESP_LOGI(TAG, "¡GATO ATRAPA AL RATÓN! (X1:%d, Y1:%d) (X2:%d, Y2:%d)", g_x1, g_y1, g_x2, g_y2);
            
            // Enviar mensaje de "Juego Terminado" al bus CAN
            twai_message_t catch_msg = {0};
            catch_msg.identifier = CATCH_MSG_ID;
            catch_msg.data_length_code = 1;
            catch_msg.data[0] = 0x01; // 0x01 = GAME OVER
            
            twai_transmit(&catch_msg, portMAX_DELAY);
            ESP_LOGI(TAG, "Mensaje de 'Juego Terminado' enviado.");
        }

        // Pequeña pausa para estabilizar la tasa de refresco
        // (Ajustar si el parpadeo es mucho o la respuesta es lenta)
        vTaskDelay(pdMS_TO_TICKS(5));
    } // fin de while(!g_game_over)

    // Bucle de juego terminado
    ESP_LOGI(TAG, "Juego terminado. Entrando en bucle infinito.");
    set_pwm_coords(0, 0); // Poner puntos en (0,0)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void app_main(void)
{
    // 1. Inicializar el hardware de PWM (Lógica de Lab 3)
    init_pwm();

    // 2. Configuración General del driver TWAI (CAN)
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);

    // 3. Configuración de Temporización (Velocidad)
    [cite_start]// La práctica especifica 500 Kbps [cite: 85]
    const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    // 4. Configuración de Filtros
    // Aceptar todos los mensajes. La lógica del juego filtrará por ID.
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // 5. Instalar el driver TWAI
    ESP_LOGI(TAG, "Instalando driver TWAI...");
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "Driver instalado.");

    // 6. Iniciar el driver
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "Driver iniciado.");

    // 7. Crear la tarea principal del juego
    xTaskCreate(game_task, "game_task", 4096, NULL, 10, NULL);
}