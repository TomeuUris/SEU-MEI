/*
 * Código de ejemplo para Lab 4 - Receptor de Comandos de Juego CAN
 *
 * Este código inicializa el bus CAN a 500 Kbps y escucha mensajes
 * con los IDs de los jugadores (0x101 y 0x102). Al recibir un comando,
 * lo interpreta y lo muestra en la consola.
 */

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"

// --- Configuraciones ---
#define TX_GPIO_NUM     5
#define RX_GPIO_NUM     4

static const char *TAG = "TWAI_GAME_RECEIVER";

// --- Definiciones del Protocolo del Juego ---
#define ID_PLAYER_1 0x101
#define ID_PLAYER_2 0x102

#define CMD_UP      0
#define CMD_DOWN    1
#define CMD_LEFT    2
#define CMD_RIGHT   3

/**
 * @brief Tarea principal que recibe e interpreta los comandos del juego.
 */
static void twai_game_logic_task(void *arg)
{
    ESP_LOGI(TAG, "Tarea de juego iniciada. Esperando comandos...");

    while (1) {
        twai_message_t rx_message;

        // 1. Esperar a recibir un mensaje
        esp_err_t ret = twai_receive(&rx_message, portMAX_DELAY);

        if (ret == ESP_OK) {
            // 2. Validar el ID y la longitud de los datos
            if ((rx_message.identifier == ID_PLAYER_1 || rx_message.identifier == ID_PLAYER_2) &&
                rx_message.data_length_code >= 1 && !rx_message.rtr) {
                
                const char* player_name = (rx_message.identifier == ID_PLAYER_1) ? "Player 1" : "Player 2";
                uint8_t command = rx_message.data[0];
                const char* command_name = "UNKNOWN";

                switch (command) {
                    case CMD_UP:    command_name = "UP";    break;
                    case CMD_DOWN:  command_name = "DOWN";  break;
                    case CMD_LEFT:  command_name = "LEFT";  break;
                    case CMD_RIGHT: command_name = "RIGHT"; break;
                }

                ESP_LOGI(TAG, "Comando recibido de %s: %s (0x%02X)", player_name, command_name, command);

                // --- AQUÍ IRÍA LA LÓGICA DEL JUEGO ---
                // Por ejemplo, mover un personaje en una pantalla, etc.

            } else {
                // Mensaje con ID no esperado o formato incorrecto
                ESP_LOGW(TAG, "Mensaje ignorado. ID: 0x%03"PRIx32", DLC: %d",
                         rx_message.identifier, rx_message.data_length_code);
            }
        } else {
            ESP_LOGE(TAG, "Fallo al recibir mensaje: %s", esp_err_to_name(ret));
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "El driver no está en estado 'running'.");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void app_main(void)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
    const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    
    // Filtro: Aceptar solo los IDs de los jugadores.
    // Para este caso simple, aceptar todo sigue siendo válido y más fácil.
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_LOGI(TAG, "Instalando driver TWAI...");
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "Driver instalado.");

    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "Driver iniciado.");

    xTaskCreate(twai_game_logic_task, "twai_game_logic_task", 4096, NULL, 10, NULL);
}