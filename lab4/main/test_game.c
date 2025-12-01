/*
 * Código de implementación para Lab 4 - Juego A: Gato y Ratón
 * Implementa la Opción A (un solo ESP: ESP-XY), ADAPTADO para actuar como
 * CONTROLADOR CENTRAL, gestionando la inicialización, el movimiento (recibido
 * en 0x201/0x202) y la finalización del juego según el nuevo protocolo.
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

// --- Configuraciones (Pines PWM y CAN) ---

static const char *TAG = "CAN_CONTROLADOR_JUEGO";

#define TX_GPIO_NUM     5 // Pin CAN TX
#define RX_GPIO_NUM     4 // Pin CAN RX

#define PWM_X_PIN       23 
#define PWM_Y_PIN       22 
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_X_CHANNEL  LEDC_CHANNEL_0
#define LEDC_Y_CHANNEL  LEDC_CHANNEL_1
#define PWM_FREQ        (250000) 
#define PWM_RESOLUTION  LEDC_TIMER_8_BIT
#define MAX_PWM_VALUE   255      

// --- Protocolo CAN y Lógica del Juego ---
// IDs de Transmisión del Controlador (Baja ID = Alta Prioridad)
#define CONTROLLER_BROADCAST_ID 0x100 // Controller -> All (Game Start/End)
#define CONTROLLER_ACK_P1_ID    0x101 // Controller -> Player 1 (ACK)
#define CONTROLLER_ACK_P2_ID    0x102 // Controller -> Player 2 (ACK)

// IDs de Recepción de los Jugadores (Origen)
#define PLAYER1_TX_ID           0x201 // Player 1 (Gato) -> Controller
#define PLAYER2_TX_ID           0x202 // Player 2 (Ratón) -> Controller

#define CATCH_MSG_ID        0x200 // NO USADO, el nuevo protocolo usa 0x100/CMD_GAME_END
#define CATCH_THRESHOLD     10    
#define MOVEMENT_STEP       10    

// Códigos de Comando CAN (data[0])
#define CMD_UP           0
#define CMD_DOWN         1
#define CMD_LEFT         2
#define CMD_RIGHT        3
#define CMD_INIT         10  // Player ready
#define CMD_INIT_ACK     11  // Controller acknowledges ready
#define CMD_GAME_END     12  // Game ended, winner in data[1]
#define CMD_GAME_START   13  // Game start

// --- Parámetros de Visualización ---
#define CYCLES_PER_POINT    50      
#define DELAY_US_PER_POINT  500     
#define CAN_CHECK_INTERVAL  100     

// Almacenamiento global (volatile para acceso seguro entre tareas)
volatile int g_x1 = 50,  g_y1 = 50;  // Posición del Gato (P1)
volatile int g_x2 = 200, g_y2 = 200; // Posición del Ratón (P2)
volatile bool g_game_over = false;
volatile bool g_p1_ready = false;
volatile bool g_p2_ready = false;
volatile bool g_game_started = false;


/**
 * @brief Inicializa los dos canales PWM (X e Y).
 */
static void init_pwm(void)
{
    // ... [código de init_pwm igual] ...
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_MODE, .duty_resolution  = PWM_RESOLUTION, 
        .timer_num        = LEDC_TIMER, .freq_hz          = PWM_FREQ,       
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
    // ... [código de set_pwm_coords igual] ...
    ledc_set_duty(LEDC_MODE, LEDC_X_CHANNEL, x);
    ledc_update_duty(LEDC_MODE, LEDC_X_CHANNEL);
    ledc_set_duty(LEDC_MODE, LEDC_Y_CHANNEL, y);
    ledc_update_duty(LEDC_MODE, LEDC_Y_CHANNEL);
}

/**
 * @brief Envía un mensaje CAN (Controlador).
 */
static void send_can_message(uint32_t id, uint8_t data_code, uint8_t data_extra)
{
    twai_message_t msg = {0};
    msg.identifier = id;
    msg.data_length_code = (id == CONTROLLER_BROADCAST_ID && data_code == CMD_GAME_END) ? 2 : 1;
    msg.data[0] = data_code;
    if (msg.data_length_code == 2) {
        msg.data[1] = data_extra;
    }

    if (twai_transmit(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
        ESP_LOGI(TAG, "TX OK: ID 0x%lX, CMD %d", msg.identifier, data_code);
    } else {
        ESP_LOGE(TAG, "TX Falló: ID 0x%lX", msg.identifier);
    }
}

/**
 * @brief Aplica un comando de movimiento a las coordenadas de un jugador.
 * Usa los nuevos IDs de origen: 0x201 (P1) y 0x202 (P2).
 */
static void apply_command(uint32_t player_tx_id, uint8_t command)
{
    volatile int *x_ptr, *y_ptr;

    if (player_tx_id == PLAYER1_TX_ID) { // 0x201 (Gato)
        x_ptr = &g_x1;
        y_ptr = &g_y1;
    } else if (player_tx_id == PLAYER2_TX_ID) { // 0x202 (Ratón)
        x_ptr = &g_x2;
        y_ptr = &g_y2;
    } else {
        return; // ID no reconocido
    }

    switch (command) {
        case CMD_UP:    *y_ptr += MOVEMENT_STEP; break;
        case CMD_DOWN:  *y_ptr -= MOVEMENT_STEP; break;
        case CMD_LEFT:  *x_ptr -= MOVEMENT_STEP; break;
        case CMD_RIGHT: *x_ptr += MOVEMENT_STEP; break;
        default: return; // Comando no reconocido
    }
    
    // Asegurar que las coordenadas estén dentro del rango [0, 255]
    if (*x_ptr < 0) *x_ptr = 0;
    if (*x_ptr > MAX_PWM_VALUE) *x_ptr = MAX_PWM_VALUE;
    if (*y_ptr < 0) *y_ptr = 0;
    if (*y_ptr > MAX_PWM_VALUE) *y_ptr = MAX_PWM_VALUE;
}


/**
 * @brief Tarea principal del juego (Recepción CAN, Visualización y Lógica).
 */
static void game_task(void *arg)
{
    ESP_LOGI(TAG, "Tarea del juego iniciada. Esperando comandos de inicialización (0x201, 0x202)...");
    bool show_point_1 = true;
    twai_message_t rx_message;
    
    int display_counter = 0;
    int can_check_counter = 0;

    while (!g_game_over) {
        
        can_check_counter++;
        if (can_check_counter >= CAN_CHECK_INTERVAL) {
            can_check_counter = 0;
            
            esp_err_t ret = twai_receive(&rx_message, 0); // Timeout 0 = no bloqueante

            if (ret == ESP_OK) {
                // 🚨 Registro detallado del mensaje recibido
                char data_str[3 * 8 + 1] = {0}; 
                for (int i = 0; i < rx_message.data_length_code; i++) {
                    sprintf(&data_str[i * 3], "0x%02X ", rx_message.data[i]);
                }
                ESP_LOGI(TAG, "RX DETECTADO: ID 0x%lX, DLC %d, Data: %s",
                         rx_message.identifier, rx_message.data_length_code, data_str);
                // ----------------------------------------------------

                if (rx_message.data_length_code >= 1) {
                    uint8_t command = rx_message.data[0];

                    if (!g_game_started) {
                        // === FASE DE INICIALIZACIÓN ===
                        if (command == CMD_INIT && (rx_message.identifier == PLAYER1_TX_ID || rx_message.identifier == PLAYER2_TX_ID)) {
                            uint32_t ack_id;
                            bool* ready_flag;
                            int player_num;

                            if (rx_message.identifier == PLAYER1_TX_ID) {
                                ready_flag = (bool*)&g_p1_ready;
                                ack_id = CONTROLLER_ACK_P1_ID; // 0x101
                                player_num = 1;
                            } else {
                                ready_flag = (bool*)&g_p2_ready;
                                ack_id = CONTROLLER_ACK_P2_ID; // 0x102
                                player_num = 2;
                            }

                            if (!*ready_flag) {
                                *ready_flag = true;
                                send_can_message(ack_id, CMD_INIT_ACK, 0); // ACK al jugador
                                ESP_LOGW(TAG, "Jugador %d LISTO. P1:%d P2:%d", player_num, g_p1_ready, g_p2_ready);
                            }

                            // Iniciar el juego si ambos están listos
                            if (g_p1_ready && g_p2_ready) {
                                g_game_started = true;
                                ESP_LOGW(TAG, "Ambos jugadores listos. INICIANDO JUEGO...");
                                send_can_message(CONTROLLER_BROADCAST_ID, CMD_GAME_START, 0); // 0x100
                            }
                        }
                    } else {
                        // === FASE DE MOVIMIENTO (Juego en curso) ===
                        if ((rx_message.identifier == PLAYER1_TX_ID || rx_message.identifier == PLAYER2_TX_ID) && command <= CMD_RIGHT) {
                            apply_command(rx_message.identifier, command);
                            
                            ESP_LOGI(TAG, "Movimiento de %s cmd %d. Pos: G(%d,%d) R(%d,%d)", 
                                     rx_message.identifier == PLAYER1_TX_ID ? "0x201 (Gato)" : "0x202 (Raton)", 
                                     command, g_x1, g_y1, g_x2, g_y2);
                        }
                    }
                }
            } else if (ret != ESP_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "Fallo al recibir: %s", esp_err_to_name(ret));
            }
            
            // 3. Comprobar colisión (solo cuando verificamos CAN)
            if (g_game_started) {
                int dx = abs(g_x1 - g_x2);
                int dy = abs(g_y1 - g_y2);

                if (dx < CATCH_THRESHOLD && dy < CATCH_THRESHOLD) {
                    g_game_over = true;
                    ESP_LOGI(TAG, "¡GATO ATRAPA RATÓN! G(%d,%d) R(%d,%d)", g_x1, g_y1, g_x2, g_y2);
                    
                    // Enviar mensaje de "Juego Terminado" (Gato/P1 es el ganador: 1)
                    send_can_message(CONTROLLER_BROADCAST_ID, CMD_GAME_END, 1);
                    ESP_LOGI(TAG, "Mensaje 'Game Over' enviado.");
                }
            }
        }
        
        // 2. Actualizar el osciloscopio
        if (show_point_1) {
            set_pwm_coords((uint8_t)g_x1, (uint8_t)g_y1); // Mostrar Gato
        } else {
            set_pwm_coords((uint8_t)g_x2, (uint8_t)g_y2); // Mostrar Ratón
        }
        
        ets_delay_us(DELAY_US_PER_POINT);
        
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
    // Aceptar todos los IDs (0x100, 0x101, 0x102, 0x201, 0x202)
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); 

    ESP_LOGI(TAG, "Instalando driver TWAI...");
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "Driver instalado.");

    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "Driver iniciado.");

    xTaskCreate(game_task, "game_task", 4096, NULL, 10, NULL);
}