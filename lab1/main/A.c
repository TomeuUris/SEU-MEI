/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "example";

/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define LED_GPIO CONFIG_BLINK_GPIO
#define BUTTON_GPIO 0

static uint8_t s_led_state = 0;
static uint8_t s_button_last_state = 0;

static void toggle_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(LED_GPIO, s_led_state);
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to toggle GPIO LED!");
    gpio_reset_pin(LED_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

static void configure_button(void)
{
    ESP_LOGI(TAG, "Example configured to use GPIO button!");
    gpio_reset_pin(BUTTON_GPIO);
    /* Set the GPIO as a input */
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    /* Set pull-up mode */
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_PULLDOWN);
}

static bool check_button_press(void)
{
    /* Read the button state */
    uint8_t s_button_current_state = gpio_get_level(BUTTON_GPIO);
    ESP_LOGI(TAG, "Button state: %d", s_button_current_state);
    
    /* Check for rising edge (button press) */
    if (s_button_last_state == 0 && s_button_current_state == 1) {
        s_button_last_state = s_button_current_state;
        return true;
    }
    
    /* Update last state */
    s_button_last_state = s_button_current_state;
    return false;
}


void app_main(void)
{

    /* Configure the LED GPIO */
    configure_led();

    /* Configure the button GPIO */
    configure_button();

    while (1) {
        /* Check if button was pressed (rising edge) */
        if(check_button_press()){
            /* Toggle the LED state */
            s_led_state = !s_led_state;
            ESP_LOGI(TAG, "Button pressed! LED state: %s", s_led_state ? "ON" : "OFF");
            toggle_led();

            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
