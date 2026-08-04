/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "me_tell_me_a_joke.h"
#include "me_sing_a_song.h"
#include "me_highest_volume.h"
#include "me_lowest_volume.h"
#include "me_increase_volume.h"
#include "me_decrease_the_volume.h"

#include "esp_board_init.h"
#include "wake_up_prompt_tone.h"
#include "speech_commands_action.h"

extern int detect_flag;

typedef struct {
    char* name;
    const uint16_t* data;
    int length;
} dac_audio_item_t;

#include "led_strip.h"

// Default built-in WS2812 RGB LED pin on ESP32-S3 DevKit boards (GPIO 48)
// Modify BUILTIN_PIXEL_LED_GPIO below if your specific board uses another pin (e.g. 21, 38, or 19)
#ifndef BUILTIN_PIXEL_LED_GPIO
#define BUILTIN_PIXEL_LED_GPIO 48
#endif

static led_strip_handle_t s_led_strip = NULL;

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
    led_set_color(0, 0, 255);
}

void led_set_green(void)
{
    led_set_color(0, 255, 0);
}

void led_set_off(void)
{
    if (s_led_strip) {
        led_strip_clear(s_led_strip);
    }
}

dac_audio_item_t playlist[] = {
    // {"ie_kaiji.h", (uint16_t*)ie_kaiji, sizeof(ie_kaiji)},
    {"wake_up_prompt_tone", (uint16_t*)wake_up_prompt_tone, sizeof(wake_up_prompt_tone)},
    {"me_tell_me_a_joke", (uint16_t*)me_tell_me_a_joke, sizeof(me_tell_me_a_joke)},
    {"me_sing_a_song", (uint16_t*)me_sing_a_song, sizeof(me_sing_a_song)},
    {"me_highest_volume", (uint16_t*)me_highest_volume, sizeof(me_highest_volume)},
    {"me_lowest_volume", (uint16_t*)me_lowest_volume, sizeof(me_lowest_volume)},
    {"me_increase_volume", (uint16_t*)me_increase_volume, sizeof(me_increase_volume)},
    {"me_decrease_the_volume", (uint16_t*)me_decrease_the_volume, sizeof(me_decrease_the_volume)},

};

void wake_up_action(void)
{
    printf("[WAKE] 'HI ESP' detected -> Pixel LED turning BLUE!\n");
    led_set_blue();
    esp_audio_play((int16_t *)(playlist[0].data), playlist[0].length, portMAX_DELAY);
}

void speech_commands_action(int command_id)
{
    printf("[COMMAND] Command ID %d recognized -> Pixel LED turning GREEN!\n", command_id);
    led_set_green();
    if (command_id >= 0 && command_id < (sizeof(playlist) / sizeof(playlist[0]) - 1)) {
        esp_audio_play((int16_t *)(playlist[command_id + 1].data), playlist[command_id + 1].length, portMAX_DELAY);
    }
}