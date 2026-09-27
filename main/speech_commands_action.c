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
#include "app_config.h" // Relay GPIOs, LED GPIO/colours/timer, wake word

/**
 * @file speech_commands_action.c
 * @brief Hardware side-effects: relay GPIO driving, WS2812 status LED
 *        patterns, Wi-Fi LED callbacks, and voice command -> action mapping.
 */

static led_strip_handle_t s_led_strip = NULL;
static TimerHandle_t led_off_timer = NULL;
static const char *TAG = "WIFI_LED_CB";

// Status LED colours (R, G, B). Cheat-sheet also documented in app_config.h:
//   ORANGE = Wi-Fi connecting, GREEN = connected (auto-off after
//   WIFI_LED_SUCCESS_ON_TIME_MS), RED = disconnected/failed,
//   BLUE = wake word detected (device listening).
#define LED_COLOR_CONNECTING   255, 165, 0
#define LED_COLOR_CONNECTED      0, 255, 0
#define LED_COLOR_DISCONNECT   255,   0, 0
#define LED_COLOR_WAKE           0,   0, 100

/**
 * @brief Configures the relay GPIOs as outputs and drives them OFF at boot.
 *
 * Pull-down is enabled so the relays cannot float ON while the ESP boots.
 * Relay 3 mirrors Relay 1 in set_relay_state(); mapping in app_config.h.
 */
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

/**
 * @brief Sets a relay ON/OFF: drives the GPIO and publishes state to MQTT.
 *
 * @param relay_id Relay number: 1 or 2 (Relay 3 mirrors Relay 1, no own ID).
 * @param state    1 = ON (relay energised), 0 = OFF.
 */
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

/**
 * @brief Initialises the single on-board WS2812 LED (idempotent).
 */
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

/** @brief Sets the LED to a raw R/G/B colour (no-op if not initialised). */
void led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (s_led_strip) {
        led_strip_set_pixel(s_led_strip, 0, red, green, blue);
        led_strip_refresh(s_led_strip);
    }
}

/** @brief LED BLUE: wake word detected, device is listening for a command. */
void led_set_blue(void)
{
    led_set_color(LED_COLOR_WAKE);
}

/** @brief LED GREEN: Wi-Fi connected (auto-off timer applies). */
void led_set_green(void)
{
    led_set_color(LED_COLOR_CONNECTED);
}

/** @brief LED off: idle state. */
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

/* Wi-Fi connecting in progress -> ORANGE */
void cb_wifi_connecting(void *pvParameter)
{
    if (led_off_timer) xTimerStop(led_off_timer, 0);
    ESP_LOGI(TAG, "Wi-Fi Connecting... Setting LED to Orange");
    led_set_color(LED_COLOR_CONNECTING);
}

/* Wi-Fi connected -> GREEN, then off after WIFI_LED_SUCCESS_ON_TIME_MS */
void cb_wifi_connected(void *pvParameter)
{
    ESP_LOGI(TAG, "Wi-Fi Connected! LED green for %d ms", (int)WIFI_LED_SUCCESS_ON_TIME_MS);
    led_set_color(LED_COLOR_CONNECTED);

    if (led_off_timer) {
        xTimerStart(led_off_timer, 0);
    }
}

/* Wi-Fi connection failed or dropped -> RED */
void cb_wifi_disconnected(void *pvParameter)
{
    if (led_off_timer) xTimerStop(led_off_timer, 0);
    ESP_LOGI(TAG, "Wi-Fi Disconnected/Failed! Setting LED to Red");
    led_set_color(LED_COLOR_DISCONNECT);
}

/* Registers Wi-Fi state LED callbacks and creates the one-shot LED-off timer. */
void register_wifi_led_callbacks(void)
{
    if (led_off_timer == NULL) {
        led_off_timer = xTimerCreate(
            "led_off_tmr",
            pdMS_TO_TICKS(WIFI_LED_SUCCESS_ON_TIME_MS), // one-shot: LED off after success period
            pdFALSE,             // One-shot timer
            (void*)0,
            led_off_timer_cb
        );
    }

    wifi_manager_set_callback(WM_ORDER_CONNECT_STA, &cb_wifi_connecting);
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_wifi_connected);
    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_wifi_disconnected);
}

/**
 * @brief Wake-word reaction: log line + LED turns BLUE to show the device
 *        is now listening for a voice command.
 */
void wake_up_action(void)
{
    printf("[WAKE] '%s' detected -> Pixel LED turning BLUE!\n", WAKE_WORD_PHRASE);
    led_set_blue();
}

/**
 * @brief Maps a recognised voice command ID to an action.
 *
 * The ID -> phrase mapping is registered in detect_Task() (main.c) and
 * documented in app_config.h. Keep all three in sync when adding commands.
 *
 * @param command_id MultiNet command ID (see VOICE_CMD_* in app_config.h).
 */
void speech_commands_action(int command_id)
{
    switch (command_id) {
        case VOICE_CMD_INVERTER_ON:   // "inverter on"
            set_relay_state(1, 1);
            break;
        case VOICE_CMD_INVERTER_OFF:  // "inverter off"
            set_relay_state(1, 0);
            break;
        case VOICE_CMD_RELAY2_ON:
            set_relay_state(2, 1);
            break;
        case VOICE_CMD_RELAY2_OFF:
            set_relay_state(2, 0);
            break;
        default:
            printf("[COMMAND] No action mapped for ID %d\n", command_id);
            break;
    }
}