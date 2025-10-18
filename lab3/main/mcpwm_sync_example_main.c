#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "rom/ets_sys.h" // Required for ets_delay_us

// --- Configuration ---
// PWM Setup
#define PWM_FREQ_HZ       250000            // 250 kHz, within the 200-300KHz recommendation [cite: 37]
#define PWM_RESOLUTION    LEDC_TIMER_8_BIT
#define PWM_MAX_DUTY      ((1 << 8) - 1)    // 255 for 8 bits
#define PWM_X_CHANNEL     LEDC_CHANNEL_0
#define PWM_Y_CHANNEL     LEDC_CHANNEL_1
#define PWM_TIMER         LEDC_TIMER_0
#define PWM_MODE          LEDC_LOW_SPEED_MODE

// GPIO Pins
#define PWM_X_GPIO        GPIO_NUM_0
#define PWM_Y_GPIO        GPIO_NUM_4

// --- Drawing Parameters ---
#define N_VERTICES        4                 // Number of vertices in our shape
#define POINTS_PER_SEGMENT 40               // Intermediate points to draw for each line 
#define TOTAL_DRAW_TIME_MS 32               // Desired total time to draw the full shape (< 40ms) 
// Calculate delay per point in microseconds
#define DELAY_US_PER_POINT (TOTAL_DRAW_TIME_MS * 1000) / (N_VERTICES * POINTS_PER_SEGMENT)


typedef struct {
    uint8_t x;
    uint8_t y;
} point_t;

// Coordinates for the square's vertices (0-255 for 8-bit resolution)
point_t square[N_VERTICES] = {
    {50, 50},    // Bottom-left
    {150, 50},   // Bottom-right
    {150, 150},  // Top-right
    {50, 150}    // Top-left
};


// --- Function Prototypes ---
void pwm_init_channel(ledc_channel_t channel, gpio_num_t gpio);
static inline void pwm_set_duty(ledc_channel_t channel, uint32_t duty);
void pwm_init_timer();
void draw_line(point_t p1, point_t p2);


// --- Main Application ---
void app_main(void)
{
    // Initialize PWM timer and channels
    pwm_init_timer();
    pwm_init_channel(PWM_X_CHANNEL, PWM_X_GPIO);
    pwm_init_channel(PWM_Y_CHANNEL, PWM_Y_GPIO);

    while (1) {
        // Draw the 4 lines of the square
        draw_line(square[0], square[1]); // Bottom line
        draw_line(square[1], square[2]); // Right line
        draw_line(square[2], square[3]); // Top line
        draw_line(square[3], square[0]); // Left line (to close the square)
    }
}


// --- Function Implementations ---

/**
 * @brief Draws a line between two points by interpolating and drawing intermediate points.
 */
void draw_line(point_t p1, point_t p2)
{
    // Calculate the distance (step) for each axis
    float dx = (float)(p2.x - p1.x) / POINTS_PER_SEGMENT;
    float dy = (float)(p2.y - p1.y) / POINTS_PER_SEGMENT;

    // Interpolate and set PWM duty for each intermediate point
    for (int i = 0; i < POINTS_PER_SEGMENT; i++) {
        uint32_t current_x = (uint32_t)(p1.x + (i * dx));
        uint32_t current_y = (uint32_t)(p1.y + (i * dy));

        pwm_set_duty(PWM_X_CHANNEL, current_x);
        pwm_set_duty(PWM_Y_CHANNEL, current_y);

        // A small microsecond delay is better here for smoother drawing
        ets_delay_us(DELAY_US_PER_POINT);
    }
}

/**
 * @brief Initializes a single PWM channel.
 */
void pwm_init_channel(ledc_channel_t channel, gpio_num_t gpio)
{
    ledc_channel_config_t ch_config = {
        .speed_mode     = PWM_MODE,
        .channel        = channel,
        .timer_sel      = PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = gpio,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ch_config);
}

/**
 * @brief Sets the duty cycle for a PWM channel.
 * Note: The ledc_set_duty function takes a uint32_t for duty.
 */
static inline void pwm_set_duty(ledc_channel_t channel, uint32_t duty)
{
    ledc_set_duty(PWM_MODE, channel, duty);
    ledc_update_duty(PWM_MODE, channel);
}

/**
 * @brief Initializes the main PWM timer.
 */
void pwm_init_timer()
{
    ledc_timer_config_t timer_conf = {
        .speed_mode       = PWM_MODE,
        .duty_resolution  = PWM_RESOLUTION,
        .timer_num        = PWM_TIMER,
        .freq_hz          = PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);
}