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
#include "driver/gpio.h"



#include "esp_board_init.h"
#include "wake_up_prompt_tone.h"
#include "speech_commands_action.h"

#define RELAY_1_GPIO GPIO_NUM_4
#define RELAY_2_GPIO GPIO_NUM_5


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

//initilize GPIOs for Relays
static void relay_gpio_init(void){
    gpio_config_t io_conf = {
        .pin_bit_mask =(1ULL <<RELAY_1_GPIO) | (1ULL<<RELAY_2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    //set gpios initilization at 0
    gpio_set_level(RELAY_1_GPIO, 0);
    gpio_set_level(RELAY_2_GPIO, 0);
}

void led_init(void)
{   relay_gpio_init();
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





void wake_up_action(void)
{
    printf("[WAKE] 'HI ESP' detected -> Pixel LED turning BLUE!\n");
    led_set_blue();
   
}

void speech_commands_action(int command_id)
{


    //custom GPIO Control Mapping
    switch (command_id){
        case 1:
            gpio_set_level(RELAY_1_GPIO, 1);
            printf("[RELAY 1]  ON\n");
            break;
        case 2:
            gpio_set_level(RELAY_1_GPIO, 0);
            printf("[RELAY 1]  OFF\n");
            break;
        case 3:
            gpio_set_level(RELAY_2_GPIO,1);
            printf("[RELAY 2] ON\n");
            break;
        case 4:
            gpio_set_level(RELAY_2_GPIO, 0);
            printf("[RELAY 2]  OFF\n");
            break;
        default:
            printf("[COMMAND]No GPIO action mapped for ID %d\n", command_id);
            break;
    }
}