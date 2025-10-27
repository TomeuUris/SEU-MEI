#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "driver/ledc.h"

#define X_GPIO_NUM     23
#define Y_GPIO_NUM     22
// --- PWM/LEDC config for oscilloscope XY output ---
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_X_CHANNEL          LEDC_CHANNEL_0
#define LEDC_Y_CHANNEL          LEDC_CHANNEL_1
// Use 8-bit resolution and 250kHz for oscilloscope
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT // 8 bits: 0-255
#define LEDC_MAX_DUTY           ((1 << 8) - 1)
#define LEDC_FREQUENCY          250000 // 250 kHz

// Approximate busy-wait microsecond delay (not precise, but enough for oscilloscope drawing)
static inline void busy_wait_us(uint32_t us) {
    volatile uint32_t count;
    while (us--) {
        // Tune this loop for your CPU speed if needed
        for (count = 0; count < 50; count++) {
            __asm__ volatile ("nop");
        }
    }
}

// Map game coordinates to PWM duty cycle (0-255)
static inline uint32_t map_to_duty(int value, int min, int max) {
    if (value < min) value = min;
    if (value > max) value = max;
    return (uint32_t)(((float)(value - min) / (max - min)) * 255.0f);
}
#define SCREEN_WIDTH    16
#define SCREEN_HEIGHT   8

// Draw a vertical bar (paddle) at x, centered at y_center, with height
static void draw_paddle(int x, int y_center, int height, int y_min, int y_max, int delay_time_us) {
    int y_start = y_center - height/2;
    int y_end = y_center + height/2;
    if (y_start < y_min) y_start = y_min;
    if (y_end > y_max) y_end = y_max;
    for (int y = y_start; y <= y_end; y++) {
        uint32_t duty_x = map_to_duty(x, 0, SCREEN_WIDTH-1);
        uint32_t duty_y = map_to_duty(y, 0, SCREEN_HEIGHT-1);
        ledc_set_duty(LEDC_MODE, LEDC_X_CHANNEL, duty_x);
        ledc_update_duty(LEDC_MODE, LEDC_X_CHANNEL);
        ledc_set_duty(LEDC_MODE, LEDC_Y_CHANNEL, duty_y);
        ledc_update_duty(LEDC_MODE, LEDC_Y_CHANNEL);
    busy_wait_us(delay_time_us);
    }
}

// Draw the ball as a dot (short sweep for visibility)
static void draw_ball(int x, int y, int dot_size, int delay_time_us) {
    int x_start = x - dot_size/2;
    int x_end = x + dot_size/2;
    int y_start = y - dot_size/2;
    int y_end = y + dot_size/2;
    for (int xi = x_start; xi <= x_end; xi++) {
        for (int yi = y_start; yi <= y_end; yi++) {
            uint32_t duty_x = map_to_duty(xi, 0, SCREEN_WIDTH-1);
            uint32_t duty_y = map_to_duty(yi, 0, SCREEN_HEIGHT-1);
            ledc_set_duty(LEDC_MODE, LEDC_X_CHANNEL, duty_x);
            ledc_update_duty(LEDC_MODE, LEDC_X_CHANNEL);
            ledc_set_duty(LEDC_MODE, LEDC_Y_CHANNEL, duty_y);
            ledc_update_duty(LEDC_MODE, LEDC_Y_CHANNEL);
            busy_wait_us(delay_time_us);
        }
    }
}

// --- Configuraciones CAN ---
#define TX_GPIO_NUM     5
#define RX_GPIO_NUM     4

#define PONG_CAN_ID     0x123

static const char *TAG = "PONG_GAME";

// --- Parámetros del juego ---
#define SCREEN_WIDTH    16
#define SCREEN_HEIGHT   8
#define PADDLE_HEIGHT   3

typedef enum { DIR_NONE = 0, DIR_UP, DIR_DOWN } paddle_dir_t;

typedef struct {
    int y; // posición vertical del centro de la pala
} paddle_t;

typedef struct {
    int x, y;
    int vx, vy;
} ball_t;

typedef struct {
    paddle_t p1, p2;
    ball_t ball;
    int score1, score2;
} pong_state_t;

static pong_state_t game = {
    .p1 = { .y = SCREEN_HEIGHT / 2 },
    .p2 = { .y = SCREEN_HEIGHT / 2 },
    .ball = { .x = SCREEN_WIDTH / 2, .y = SCREEN_HEIGHT / 2, .vx = 1, .vy = 1 },
    .score1 = 0,
    .score2 = 0
};

// --- Lógica de entrada por CAN ---
static void pong_can_input_task(void *arg)
{
    while (1) {
        twai_message_t rx_msg;
        if (twai_receive(&rx_msg, portMAX_DELAY) == ESP_OK) {
            if (rx_msg.identifier == PONG_CAN_ID && rx_msg.data_length_code == 2) {
                uint8_t player = rx_msg.data[0];
                paddle_dir_t dir = rx_msg.data[1];
                if (player == 1) {
                    if (dir == DIR_UP && game.p1.y > PADDLE_HEIGHT/2)
                        game.p1.y--;
                    else if (dir == DIR_DOWN && game.p1.y < SCREEN_HEIGHT-1-PADDLE_HEIGHT/2)
                        game.p1.y++;
                } else if (player == 2) {
                    if (dir == DIR_UP && game.p2.y > PADDLE_HEIGHT/2)
                        game.p2.y--;
                    else if (dir == DIR_DOWN && game.p2.y < SCREEN_HEIGHT-1-PADDLE_HEIGHT/2)
                        game.p2.y++;
                }
            }
        }
    }
}

// --- Lógica principal del juego ---
static void pong_game_task(void *arg)
{
    const int frame_delay_ms = 20; // Delay between frames
    const int ball_delay_frames = 5; // Move ball every 5 frames
    const int osc_draw_repeats = 30; // Number of times to draw per frame
    int frame = 0;
    while (1) {
        // Move ball every N frames
        if (frame % ball_delay_frames == 0) {
            game.ball.x += game.ball.vx;
            game.ball.y += game.ball.vy;

            // Rebote vertical
            if (game.ball.y <= 0 || game.ball.y >= SCREEN_HEIGHT-1)
                game.ball.vy = -game.ball.vy;

            // Rebote con pala izquierda
            if (game.ball.x == 1) {
                if (game.ball.y >= game.p1.y - PADDLE_HEIGHT/2 && game.ball.y <= game.p1.y + PADDLE_HEIGHT/2)
                    game.ball.vx = -game.ball.vx;
                else {
                    game.score2++;
                    game.ball.x = SCREEN_WIDTH/2; game.ball.y = SCREEN_HEIGHT/2;
                }
            }
            // Rebote con pala derecha
            if (game.ball.x == SCREEN_WIDTH-2) {
                if (game.ball.y >= game.p2.y - PADDLE_HEIGHT/2 && game.ball.y <= game.p2.y + PADDLE_HEIGHT/2)
                    game.ball.vx = -game.ball.vx;
                else {
                    game.score1++;
                    game.ball.x = SCREEN_WIDTH/2; game.ball.y = SCREEN_HEIGHT/2;
                }
            }
        }

        // Draw paddles and ball multiple times per frame for persistence
        for (int i = 0; i < osc_draw_repeats; i++) {
            // Left paddle (x=0)
            draw_paddle(0, game.p1.y, PADDLE_HEIGHT, 0, SCREEN_HEIGHT-1, 40);
            // Right paddle (x=SCREEN_WIDTH-1)
            draw_paddle(SCREEN_WIDTH-1, game.p2.y, PADDLE_HEIGHT, 0, SCREEN_HEIGHT-1, 40);
            // Ball (dot)
            draw_ball(game.ball.x, game.ball.y, 1, 60);
        }

        vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
        frame++;
    }
}

void app_main(void)
{
    // Inicializa CAN (TWAI)
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
    const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());

    // Inicializa LEDC para X (GPIO 23) y Y (GPIO 22)
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel_x = {
        .gpio_num = X_GPIO_NUM,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_X_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config_t ledc_channel_y = {
        .gpio_num = Y_GPIO_NUM,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_Y_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel_x);
    ledc_channel_config(&ledc_channel_y);

    // Tareas
    xTaskCreate(pong_can_input_task, "pong_can_input_task", 2048, NULL, 10, NULL);
    xTaskCreate(pong_game_task, "pong_game_task", 4096, NULL, 5, NULL);
}