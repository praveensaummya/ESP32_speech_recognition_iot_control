#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configures relay GPIOs as outputs and initialises them to OFF.
 * Call once from app_main before any set_relay_state() call.
 */
void relay_gpio_init(void);

/**
 * @brief Sets a relay ON/OFF, drives the GPIO, and publishes state to MQTT.
 *
 * @param relay_id Relay number: 1 or 2. (Relay 3 mirrors Relay 1 and has no
 *                 ID of its own — see GPIO map in app_config.h.)
 * @param state    1 = ON, 0 = OFF.
 */
void set_relay_state(int relay_id, int state);

/**
 * @brief Initialises the on-board WS2812 status LED (safe to call twice).
 */
void led_init(void);

/** @brief Sets the LED to a raw R/G/B colour (no-op if not initialised). */
void led_set_color(uint8_t red, uint8_t green, uint8_t blue);

/** @brief LED BLUE = wake word detected, listening for a voice command. */
void led_set_blue(void);

/** @brief LED GREEN = Wi-Fi connected (auto-off timer applies). */
void led_set_green(void);

/** @brief LED off = idle. */
void led_set_off(void);

/** @brief Wake-word reaction: log line + LED turns BLUE. */
void wake_up_action(void);

/**
 * @brief Dispatches a recognised voice command ID to its action.
 *
 * @param command_id MultiNet command ID (VOICE_CMD_* map in app_config.h).
 */
void speech_commands_action(int command_id);

#ifdef __cplusplus
}
#endif