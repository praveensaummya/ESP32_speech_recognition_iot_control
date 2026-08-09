#pragma once

#include <stdint.h>
#include <stdbool.h>

// Relay and Hardware APIs
void relay_gpio_init(void);
void set_relay_state(int relay_id, int state);

// LED APIs
void led_init(void);
void led_set_color(uint8_t red, uint8_t green, uint8_t blue);
void led_set_blue(void);
void led_set_green(void);
void led_set_off(void);

// Voice Action APIs
void wake_up_action(void);
void speech_commands_action(int command_id);