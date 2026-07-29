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
#include "me_play_news_channel.h"
#include "me_turn_on_my_soundbox.h"
#include "me_turn_off_my_soundbox.h"
#include "me_highest_volume.h"
#include "me_lowest_volume.h"
#include "me_increase_volume.h"
#include "me_decrease_the_volume.h"
#include "me_turn_on_the_TV.h"
#include "me_turn_off_the_TV.h"
#include "me_make_me_a_tea.h"
#include "me_make_me_a_coffee.h"
#include "me_turn_on_the_light.h"
#include "me_turn_off_the_light.h"
#include "me_red_color.h"
#include "me_green_color.h"
#include "me_turn_on_all_the_light.h"
#include "me_turn_off_all_the_light.h"
#include "me_turn_on_the_air_conditioner.h"
#include "me_turn_off_the_air_conditioner.h"
#include "me_16_degress.h"
#include "me_17_degrees.h"
#include "me_18_degrees.h"
#include "me_19_degrees.h"
#include "me_20_degrees.h"
#include "me_21_degrees.h"
#include "me_22_degrees.h"
#include "me_23_degrees.h"
#include "me_24_degrees.h"
#include "me_25_degrees.h"
#include "me_26_degrees.h"
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
    {"me_play_news_channel", (uint16_t*)me_play_news_channel, sizeof(me_play_news_channel)},
    {"me_turn_on_my_soundbox", (uint16_t*)me_turn_on_my_soundbox, sizeof(me_turn_on_my_soundbox)},
    {"me_turn_off_my_soundbox", (uint16_t*)me_turn_off_my_soundbox, sizeof(me_turn_off_my_soundbox)},
    {"me_highest_volume", (uint16_t*)me_highest_volume, sizeof(me_highest_volume)},
    {"me_lowest_volume", (uint16_t*)me_lowest_volume, sizeof(me_lowest_volume)},
    {"me_increase_volume", (uint16_t*)me_increase_volume, sizeof(me_increase_volume)},
    {"me_decrease_the_volume", (uint16_t*)me_decrease_the_volume, sizeof(me_decrease_the_volume)},
    {"me_turn_on_the_TV", (uint16_t*)me_turn_on_the_TV, sizeof(me_turn_on_the_TV)},
    {"me_turn_off_the_TV", (uint16_t*)me_turn_off_the_TV, sizeof(me_turn_off_the_TV)},
    {"me_make_me_a_tea", (uint16_t*)me_make_me_a_tea, sizeof(me_make_me_a_tea)},
    {"me_make_me_a_coffee", (uint16_t*)me_make_me_a_coffee, sizeof(me_make_me_a_coffee)},
    {"me_turn_on_the_light", (uint16_t*)me_turn_on_the_light, sizeof(me_turn_on_the_light)},
    {"me_turn_off_the_light", (uint16_t*)me_turn_off_the_light, sizeof(me_turn_off_the_light)},
    {"me_red_color", (uint16_t*)me_red_color, sizeof(me_red_color)},
    {"me_green_color", (uint16_t*)me_green_color, sizeof(me_green_color)},
    {"me_turn_on_all_the_light", (uint16_t*)me_turn_on_all_the_light, sizeof(me_turn_on_all_the_light)},
    {"me_turn_off_all_the_light", (uint16_t*)me_turn_off_all_the_light, sizeof(me_turn_off_all_the_light)},
    {"me_turn_on_the_air_conditioner", (uint16_t*)me_turn_on_the_air_conditioner, sizeof(me_turn_on_the_air_conditioner)},
    {"me_turn_off_the_air_conditioner", (uint16_t*)me_turn_off_the_air_conditioner, sizeof(me_turn_off_the_air_conditioner)},
    {"me_16_degress", (uint16_t*)me_16_degress, sizeof(me_16_degress)},
    {"me_17_degrees", (uint16_t*)me_17_degrees, sizeof(me_17_degrees)},
    {"me_18_degrees", (uint16_t*)me_18_degrees, sizeof(me_18_degrees)},
    {"me_19_degrees", (uint16_t*)me_19_degrees, sizeof(me_19_degrees)},
    {"me_20_degrees", (uint16_t*)me_20_degrees, sizeof(me_20_degrees)},
    {"me_21_degrees", (uint16_t*)me_21_degrees, sizeof(me_21_degrees)},
    {"me_22_degrees", (uint16_t*)me_22_degrees, sizeof(me_22_degrees)},
    {"me_23_degrees", (uint16_t*)me_23_degrees, sizeof(me_23_degrees)},
    {"me_24_degrees", (uint16_t*)me_24_degrees, sizeof(me_24_degrees)},
    {"me_25_degrees", (uint16_t*)me_25_degrees, sizeof(me_25_degrees)},
    {"me_26_degrees", (uint16_t*)me_26_degrees, sizeof(me_26_degrees)},
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