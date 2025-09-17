/* Caso B: Toggle LED only with long press (>0.5s)
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "case_b";

#define LED_GPIO 4
#define BUTTON_GPIO 0
#define LONG_PRESS_TIME_MS 500 // Minimum press time required (0.5 seconds)

// States
typedef enum {
    IDLE,             // Waiting for button press
    BUTTON_PRESSED,   // Button pressed, checking duration
    BUTTON_RELEASED   // Long press detected, waiting for button release
} button_state_t;

// Global variables
static uint8_t s_led_state = 0;                     // Tracks LED state (0=OFF, 1=ON)
static button_state_t current_state = IDLE;   // Current state machine state
static TickType_t press_start_time = 0;             // Time when button was first pressed

// Function to update LED state
static void toggle_led(void)
{
    gpio_set_level(LED_GPIO, s_led_state);
}

// Configure LED GPIO pin
static void configure_led(void)
{
    ESP_LOGI(TAG, "Configuring LED on GPIO%d", LED_GPIO);
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

// Configure button GPIO pin
static void configure_button(void)
{
    ESP_LOGI(TAG, "Configuring button on GPIO%d", BUTTON_GPIO);
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_PULLDOWN);
}

void app_main(void)
{
    // Initialize hardware
    configure_led();
    configure_button();

    ESP_LOGI(TAG, "Starting state machine - Case B (long press)");

    // Main polling loop
    while (1) {
        uint8_t button_level = gpio_get_level(BUTTON_GPIO); // Read current button state
        TickType_t current_time = xTaskGetTickCount();      // Get current system time
        
        // State machine logic
        switch (current_state) {
            case IDLE:
                if (button_level == 1) {
                    // Button pressed, start timing
                    current_state = BUTTON_PRESSED;
                    press_start_time = current_time;
                    ESP_LOGI(TAG, "Button pressed, starting timer...");
                }
                break;
                
            case BUTTON_PRESSED:
                if (button_level == 0) {
                    // Button released before minimum time
                    current_state = IDLE;
                    ESP_LOGI(TAG, "Button released too soon");
                } else {
                    // Check if enough time has passed
                    TickType_t elapsed = (current_time - press_start_time) * portTICK_PERIOD_MS;
                    if (elapsed >= LONG_PRESS_TIME_MS) {
                        // Valid long press - change to release waiting state
                        current_state = BUTTON_RELEASED;
                        s_led_state = !s_led_state;
                        toggle_led();
                        ESP_LOGI(TAG, "Long press detected! LED: %s", s_led_state ? "ON" : "OFF");
                    }
                }
                break;
                
            case BUTTON_RELEASED:
                if (button_level == 0) {
                    // Button released, return to initial state
                    current_state = IDLE;
                    ESP_LOGI(TAG, "Button released, returning to initial state");
                }
                break;
        }
        
        // Small delay to avoid excessive CPU usage
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}