#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "led_strip.h"

#include "esp_board_init.h"
#include "wake_up_prompt_tone.h"
#include "speech_commands_action.h"
#include "mqtt_server.h" 

#include "wifi_manager.h"
#include "esp_log.h"

#define RELAY_1_GPIO GPIO_NUM_4
#define RELAY_2_GPIO GPIO_NUM_5
#define RELAY_3_GPIO GPIO_NUM_13

#ifndef BUILTIN_PIXEL_LED_GPIO
#define BUILTIN_PIXEL_LED_GPIO 48
#endif

static led_strip_handle_t s_led_strip = NULL;
static TimerHandle_t led_off_timer = NULL;
static const char *TAG = "WIFI_LED_CB";

// 1. Standalone Relay Initialization
void relay_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_1_GPIO) | (1ULL << RELAY_2_GPIO) | (1ULL << RELAY_3_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Set initial GPIO state to OFF
    gpio_set_level(RELAY_1_GPIO, 0);
    gpio_set_level(RELAY_2_GPIO, 0);
    gpio_set_level(RELAY_3_GPIO, 0);
    printf("[RELAY] GPIO %d , GPIO %d , and GPIO %d initialized to OFF\n", RELAY_1_GPIO, RELAY_2_GPIO, RELAY_3_GPIO);
}

// 2. Unified Relay Control Function (Drives HW & Cloud MQTT)
void set_relay_state(int relay_id, int state)
{
    gpio_num_t pin;

    if (relay_id == 1) {
        pin = RELAY_1_GPIO;
    } else if (relay_id == 2) {
        pin = RELAY_2_GPIO;
    } else {
        printf("[RELAY ERROR] Invalid relay ID %d\n", relay_id);
        return;
    }

    // Set physical hardware level
    gpio_set_level(pin, state ? 1 : 0);
    printf("[RELAY %d] -> %s\n", relay_id, state ? "ON" : "OFF");

    if (relay_id == 1) {
        gpio_set_level(RELAY_3_GPIO, state ? 1 : 0);
        printf("[RELAY 3] -> %s (Mirrored with Relay 1)\n", state ? "ON" : "OFF");
    }

    // Publish state update to Cloud MQTT topic
    mqtt_publish_relay_status(relay_id, state);
}

// 3. Standalone LED Initialization
void led_init(void)
{
    if (s_led_strip != NULL) {
        return;
    }
    led_strip_config_t strip_config = {
        .strip_gpio_num = BUILTIN_PIXEL_LED_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret == ESP_OK && s_led_strip) {
        led_strip_clear(s_led_strip);
        printf("[LED] Built-in WS2812 Pixel LED initialized on GPIO %d\n", BUILTIN_PIXEL_LED_GPIO);
    } else {
        printf("[LED] Failed to initialize WS2812 LED on GPIO %d (err: 0x%x)\n", BUILTIN_PIXEL_LED_GPIO, ret);
    }
}

void led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (s_led_strip) {
        led_strip_set_pixel(s_led_strip, 0, red, green, blue);
        led_strip_refresh(s_led_strip);
    }
}

void led_set_blue(void)
{
    led_set_color(0, 0, 100);
}

void led_set_green(void)
{
    led_set_color(0, 100, 0);
}

void led_set_off(void)
{
    if (s_led_strip) {
        led_strip_clear(s_led_strip);
    }
}

/* Callback to turn off the Pixel LED after success timer expires */
static void led_off_timer_cb(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Success timer expired. Turning off Pixel LED.");
    led_set_off();
}

/* Triggered when connecting starts -> YELLOW / ORANGE */
void cb_wifi_connecting(void *pvParameter)
{
    if (led_off_timer) xTimerStop(led_off_timer, 0);
    ESP_LOGI(TAG, "Wi-Fi Connecting... Setting LED to Yellow");
    led_set_color(255, 165, 0); 
}

/* Triggered when connection succeeds -> GREEN for 3 seconds, then OFF */
void cb_wifi_connected(void *pvParameter)
{
    ESP_LOGI(TAG, "Wi-Fi Connected! Setting LED to Green for 5 seconds");
    led_set_color(0, 255, 0); 

    if (led_off_timer) {
        xTimerStart(led_off_timer, 0);
    }
}

/* Triggered when connection fails or disconnects -> RED */
void cb_wifi_disconnected(void *pvParameter)
{
    if (led_off_timer) xTimerStop(led_off_timer, 0);
    ESP_LOGI(TAG, "Wi-Fi Disconnected/Failed! Setting LED to Red");
    led_set_color(255, 0, 0); 
}

/* Register callbacks & initialize the 3-second timer */
void register_wifi_led_callbacks(void)
{
    if (led_off_timer == NULL) {
        led_off_timer = xTimerCreate(
            "led_off_tmr",
            pdMS_TO_TICKS(5000), // 3 Seconds
            pdFALSE,             // One-shot timer
            (void*)0,
            led_off_timer_cb
        );
    }

    wifi_manager_set_callback(WM_ORDER_CONNECT_STA, &cb_wifi_connecting);
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_wifi_connected);
    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_wifi_disconnected);
}

void wake_up_action(void)
{
    printf("[WAKE] 'HI ESP' detected -> Pixel LED turning BLUE!\n");
    led_set_blue();
}

// 4. Voice Command Handler
void speech_commands_action(int command_id)
{
    switch (command_id) {
        case 1:
            set_relay_state(1, 1); // Relay 1 ON
            printf("[RELAY 1]  ON\n");
            break;
        case 2:
            set_relay_state(1, 0); // Relay 1 OFF
            printf("[RELAY 1]  OFF\n");
            break;
        case 3:
            set_relay_state(2, 1); // Relay 2 ON
            printf("[RELAY 2]  ON\n");
            break;
        case 4:
            set_relay_state(2, 0); // Relay 2 OFF
            printf("[RELAY 2]  OFF\n");
            break;
        default:
            printf("[COMMAND] No action mapped for ID %d\n", command_id);
            break;
    }
}