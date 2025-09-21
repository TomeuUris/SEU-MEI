/* Case C: Long press (>0.5s) activates fast blinking for 10s
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "case_c";

#define LED_GPIO 4
#define BUTTON_GPIO 0
#define LONG_PRESS_TIME_MS 500     // Minimum press time required (0.5 seconds)
#define BLINK_DURATION_MS 10000    // Total blinking duration (10 seconds)
#define BLINK_PERIOD_MS 200        // Blinking period (100ms ON, 100ms OFF)

// State machine states
typedef enum {
    IDLE,                           // Idle state, LED off, waiting for press
    BUTTON_PRESSED,                     // Button pressed, validating duration
    BUTTON_RELEASED,                // Blinking active with button still pressed
    BLINKING,                       // Blinking active with button released
    BLINKING_BUTTON_PRESSED,        // Detecting possible cancellation during blink
    BLINKING_ENDED_BUTTON_RELEASED        // Cancellation confirmed, waiting for release
} button_state_t;

// Global variables
static uint8_t s_led_state = 0;                    // Current LED state (0=OFF, 1=ON)
static button_state_t current_state = IDLE;        // Current state machine state
static TickType_t press_start_time = 0;            // Timestamp when button was pressed
static TickType_t blink_start_time = 0;            // Timestamp when blinking started
static TickType_t last_blink_time = 0;             // Timestamp of last blink toggle

// Function to set LED state
static void set_led(uint8_t state)
{
    gpio_set_level(LED_GPIO, state);
    s_led_state = state;
}

// Configure LED GPIO pin
static void configure_led(void)
{
    ESP_LOGI(TAG, "Configuring LED on GPIO%d", LED_GPIO);
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    set_led(0); // Initialize LED off
}

// Configure button GPIO pin
static void configure_button(void)
{
    ESP_LOGI(TAG, "Configuring button on GPIO%d", BUTTON_GPIO);
    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLDOWN_ONLY);
}

static TickType_t getElapsed(TickType_t current_time, TickType_t start_time)
{
    return (current_time - start_time) * portTICK_PERIOD_MS;
}

static bool checkBlinkExcededDuration(TickType_t current_time)
{
    return getElapsed(current_time, blink_start_time) >= BLINK_DURATION_MS;
}

static bool checkBlinkToggleTime(TickType_t current_time)
{
    return getElapsed(current_time, last_blink_time) >= (BLINK_PERIOD_MS / 2);
}

static bool checkLongPress(TickType_t current_time)
{
    return getElapsed(current_time, press_start_time) >= LONG_PRESS_TIME_MS;
}

void app_main(void)
{
    // Initialize
    configure_led();
    configure_button();

    ESP_LOGI(TAG, "Starting state machine - Case C (blinking with long press cancellation)");
    
    // Main polling loop
    while (1) {
        uint8_t button_level = gpio_get_level(BUTTON_GPIO);  // Read current button state
        TickType_t current_time = xTaskGetTickCount();       // Get current system time
        
        // State machine logic
        switch (current_state) {
            case IDLE:
                // Idle state: LED off, waiting for button press
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
                    ESP_LOGI(TAG, "Button released too early");
                } else {
                    // Check if enough time has passed
                    if (checkLongPress(current_time)) {
                        // Valid long press - start blinking IMMEDIATELY
                        current_state = BUTTON_RELEASED;
                        blink_start_time = current_time;
                        last_blink_time = current_time;
                        set_led(1); // Start with LED on
                        ESP_LOGI(TAG, "Long press detected! Starting blinking immediately");
                    }
                }
                break;
                
            case BUTTON_RELEASED:
                // Blinking active with initial button still pressed
                // Check first if 10 seconds have passed
                if (checkBlinkExcededDuration(current_time)) {
                    // End blinking regardless of button state
                    set_led(0);
                    current_state = BLINKING_ENDED_BUTTON_RELEASED;
                    ESP_LOGI(TAG, "Blinking finished after 10 seconds, LED off");
                } else if (button_level == 0) {
                    // Button released, continue blinking
                    current_state = BLINKING;
                    ESP_LOGI(TAG, "Button released, continuing blinking");
                } else {
                    // Button still pressed from initial activation
                    // To cancel, must release and press again for >0.5s
                    // Only continue blinking
                    if (checkBlinkToggleTime(current_time)) {
                        s_led_state = !s_led_state;
                        set_led(s_led_state);
                        last_blink_time = current_time;
                    }
                }
                break;
                
            case BLINKING:
                // Blinking active with button released
                // Check if button has been pressed during blinking
                if (button_level == 1) {
                    current_state = BLINKING_BUTTON_PRESSED;
                    press_start_time = current_time;
                    ESP_LOGI(TAG, "Button pressed during blinking...");
                } else {
                    // Check if 10 seconds have passed
                    if (checkBlinkExcededDuration(current_time)) {
                        // End blinking
                        set_led(0); // Turn off LED
                        current_state = IDLE;
                        ESP_LOGI(TAG, "Blinking finished, LED off");
                    } else {
                        // Continue blinking
                        if (checkBlinkToggleTime(current_time)) {
                            s_led_state = !s_led_state;
                            set_led(s_led_state);
                            last_blink_time = current_time;
                        }
                    }
                }
                break;
                
            case BLINKING_BUTTON_PRESSED:
                // Detecting possible cancellation during blinking
                if (button_level == 0) {
                    // Button released before minimum time, continue blinking
                    current_state = BLINKING;
                    ESP_LOGI(TAG, "Button released too early, continuing blinking");
                } else {
                    // Check if enough time has passed to cancel
                    if (checkLongPress(current_time)) {
                        // Valid long press - cancel blinking
                        set_led(0); // Turn off LED
                        current_state = BLINKING_ENDED_BUTTON_RELEASED;
                        ESP_LOGI(TAG, "Long press detected! Cancelling blinking...");
                    } else {
                        // Continue blinking while checking press duration
                        if (checkBlinkToggleTime(current_time)) {
                            s_led_state = !s_led_state;
                            set_led(s_led_state);
                            last_blink_time = current_time;
                        }
                    }
                }
                break;
                
            case BLINKING_ENDED_BUTTON_RELEASED:
                // Cancellation confirmed, waiting for button release
                if (button_level == 0) {
                    // Button released, cancel blinking
                    current_state = IDLE;
                    ESP_LOGI(TAG, "Button released, blinking cancelled, LED off");
                }
                break;
        }
        // Small delay to avoid excessive CPU usage (10ms polling rate)
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}