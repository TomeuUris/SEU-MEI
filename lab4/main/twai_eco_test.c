/*
 * Código de ejemplo para Lab 4 - Test del Bus CAN (Eco)
 * Basado en los requisitos de la práctica  y la API de ESP-IDF.
 *
 * Este código inicializa el bus CAN a 500 Kbps  y entra en un bucle
 * donde escucha mensajes. Cualquier mensaje recibido es inmediatamente
 * retransmitido al bus (eco).
 */

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h" // API para el bus CAN (compatible con ESP32-C6)

// --- Configuraciones ---

// !! IMPORTANTE: AJUSTA ESTOS PINES !!
// Consulta el pinout de tu placa ESP32-C6 y el datasheet del transceptor [cite: 79]
// para saber qué pines GPIO has conectado a D(TXD) y R(RXD) del transceptor TTL-CAN.
#define TX_GPIO_NUM     5
#define RX_GPIO_NUM     4

static const char *TAG = "TWAI_ECHO_TEST";

/**
 * @brief Tarea principal que recibe y hace eco de mensajes CAN.
 *
 * Esta tarea bloquea su ejecución esperando recibir un mensaje CAN.
 * Cuando un mensaje es recibido, lo imprime en la consola y
 * lo transmite de vuelta al bus CAN.
 */
static void twai_echo_task(void *arg)
{
    ESP_LOGI(TAG, "Tarea de eco iniciada. Esperando mensajes...");

    while (1) {
        twai_message_t rx_message;

        // 1. Esperar a recibir un mensaje (bloqueante)
        esp_err_t ret = twai_receive(&rx_message, portMAX_DELAY);

        if (ret == ESP_OK) {
            // Mensaje recibido correctamente
            ESP_LOGI(TAG, "Mensaje recibido! ID: 0x%03"PRIx32", DLC: %d",
                     rx_message.identifier, rx_message.data_length_code);

            // Imprimir datos si no es una trama RTR (Remote Transmission Request)
            if (!rx_message.rtr) {
                printf("  Datos: ");
                for (int i = 0; i < rx_message.data_length_code; i++) {
                    printf("0x%02X ", rx_message.data[i]);
                }
                printf("\n");
            }

            // 2. Hacer el eco: retransmitir el mismo mensaje 
            ESP_LOGI(TAG, "Enviando eco...");
            ret = twai_transmit(&rx_message, portMAX_DELAY);

            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Eco enviado correctamente.");
            } else {
                ESP_LOGE(TAG, "Fallo al enviar el eco: %s", esp_err_to_name(ret));
            }

        } else {
            // Error al recibir (ej. si se detiene el driver)
            ESP_LOGE(TAG, "Fallo al recibir mensaje: %s", esp_err_to_name(ret));
            // Esperar un poco antes de reintentar si algo sale mal
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void app_main(void)
{
    // 1. Configuración General del driver
    // Define los pines TX/RX y el modo de operación (Normal) [cite: 36]
    // Asegúrate de que TX_GPIO_NUM y RX_GPIO_NUM sean correctos.
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);

    // 2. Configuración de Temporización (Velocidad)
    // La práctica especifica una velocidad de 500 Kbps 
    const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    // 3. Configuración de Filtros
    // Para la prueba de eco, queremos aceptar todos los mensajes 
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // 4. Instalar el driver TWAI
    ESP_LOGI(TAG, "Instalando driver TWAI...");
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "Driver instalado.");

    // 5. Iniciar el driver
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "Driver iniciado.");

    // 6. Crear la tarea de eco
    // Esta tarea se encargará de recibir y retransmitir los mensajes
    xTaskCreate(twai_echo_task, "twai_echo_task", 4096, NULL, 10, NULL);
}