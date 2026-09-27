#pragma once

/**
 * @file app_config.h
 * @brief Central configuration for the ESP32-S3 Speech Command Inverter project.
 *
 * EVERY tunable value used by the application lives here so that a new
 * developer can find and change settings in ONE place, with an explanation
 * of what each value does and what depends on it.
 *
 * NOTE: MQTT broker credentials are NOT in this file. They are read at
 * runtime from NVS flash, falling back to defaults defined in
 * `main/secrets.h` (local-only, git-ignored — see `secrets.h.example`).
 *
 * Changing values here requires a full rebuild (`idf.py build`) — none of
 * these are runtime-configurable (except MQTT broker, stored in NVS and
 * editable at runtime via POST /api/config/mqtt).
 */

#include "driver/gpio.h"

// ===========================================================================
// 1. HTTP API SERVER (dedicated REST server for the JCON mobile app)
// ===========================================================================

/**
 * Port of the dedicated REST API server started in cb_connection_ok().
 * IMPORTANT: This value is referenced by:
 *   - mDNS advertisement (APP_MDNS_SERVICE_PORT below)
 *   - the mobile app, which resolves "esp32-inverter.local" and connects here
 *   - GET /api/status response body (advertises the port to the app)
 * If you change this, update all three (they all use this define).
 */
#define APP_HTTP_API_PORT              8080

/**
 * Internal control port for esp_http_server. Must DIFFER from the port used
 * by the esp-wifi-manager captive portal, which uses the default 32768.
 * Changing the API port does NOT require changing this, unless it collides.
 */
#define APP_HTTP_CTRL_PORT             32769

// ===========================================================================
// 2. mDNS (device discovery — the mobile app finds the board via this)
// ===========================================================================

/**
 * mDNS hostname. The mobile app resolves "http://<hostname>.local:8080" to
 * find the device on the LAN. Changing this breaks device discovery unless
 * the app is updated too.
 */
#define APP_MDNS_HOSTNAME              "esp32-inverter"

/** Human-readable instance name shown in mDNS browser tools (e.g. avahi). */
#define APP_MDNS_INSTANCE_NAME         "ESP32-S3 Smart Inverter Controller"

/** Service name/type/protocol advertised so browsers list the web API. */
#define APP_MDNS_SERVICE_NAME          "ESP32-WebServer"
#define APP_MDNS_SERVICE_TYPE          "_http"
#define APP_MDNS_SERVICE_PROTO         "_tcp"

/** Port advertised via mDNS — must match APP_HTTP_API_PORT. */
#define APP_MDNS_SERVICE_PORT          APP_HTTP_API_PORT

// ===========================================================================
// 3. MQTT CLOUD (topics, QoS, NVS storage keys)
// ===========================================================================
// The broker URI/username/password are runtime-configurable (stored in NVS,
// editable via POST /api/config/mqtt) with fallback defaults in secrets.h.

/** Topic the device SUBSCRIBES to for relay commands  {"relay":1,"state":1}. */
#define MQTT_TOPIC_RELAY_COMMAND       "device/relays/command"
/** Topic the device PUBLISHES relay state updates to. */
#define MQTT_TOPIC_RELAY_STATUS        "device/relays/status"
/** Topic the device SUBSCRIBES to for voice toggle {"voice_enabled":true}. */
#define MQTT_TOPIC_VOICE_COMMAND       "device/voice/command"
/** Topic the device PUBLISHES voice-recognition state to. */
#define MQTT_TOPIC_VOICE_STATUS        "device/voice/status"

/** MQTT QoS used for all subscriptions and publishes (1 = at-least-once). */
#define MQTT_QOS                       1

// --- NVS storage keys (flash-persisted settings) ---------------------------
// WARNING: changing these makes previously saved settings unreadable —
// existing devices would fall back to defaults. Erase NVS if you rename keys.

/** NVS namespace holding the MQTT broker settings. */
#define NVS_NAMESPACE_MQTT             "mqtt_config"
#define NVS_KEY_MQTT_URI               "broker_uri"   /**< Broker URL, e.g. mqtts://host:8883 */
#define NVS_KEY_MQTT_USER              "broker_user"  /**< Broker username */
#define NVS_KEY_MQTT_PASS              "broker_pass"  /**< Broker password */

/** NVS namespace holding the voice-recognition on/off setting. */
#define NVS_NAMESPACE_VOICE            "voice_cfg"
#define NVS_KEY_VOICE_ENABLED          "voice_enabled" /**< uint8: 1 = ON, 0 = OFF */

// ===========================================================================
// 4. RELAYS (physical GPIO mapping)
// ===========================================================================
// Voice commands and the MQTT/HTTP API address relays by ID (1, 2, ...).
// Mapping:  Relay ID 1 -> RELAY_1_GPIO,  Relay ID 2 -> RELAY_2_GPIO.
// NOTE: RELAY_3_GPIO is NOT independently controllable — it always MIRRORS
// Relay 1 (see set_relay_state() in speech_commands_action.c). It is a
// second physical output wired to the same load group, so it has no ID.

#define RELAY_1_GPIO                   GPIO_NUM_4   /**< Relay ID 1 ("inverter on/off") */
#define RELAY_2_GPIO                   GPIO_NUM_5   /**< Relay ID 2 */
#define RELAY_3_GPIO                   GPIO_NUM_13  /**< Mirror of Relay ID 1 — no own ID */

// ===========================================================================
// 5. STATUS LED (built-in WS2812 RGB LED — Wi-Fi & wake-word indications)
// ===========================================================================

/** GPIO of the on-board WS2812 addressable LED (ESP32-S3 DevKitC default). */
#ifndef BUILTIN_PIXEL_LED_GPIO
#define BUILTIN_PIXEL_LED_GPIO         48
#endif

/**
 * How long the LED stays GREEN after a successful Wi-Fi connect before it
 * switches off again (one-shot FreeRTOS timer).
 * Colour cheat-sheet: ORANGE = connecting, GREEN = connected (then off),
 * RED = disconnected/failed, BLUE = wake word "HI ESP" detected.
 */
#define WIFI_LED_SUCCESS_ON_TIME_MS    6000   /* ms — keep comments in sync */

// ===========================================================================
// 6. VOICE RECOGNITION (wake word + MultiNet command detection)
// ===========================================================================

/**
 * Wake word phrase (WakeNet model). Must match the model flashed in the
 * "model" partition. On detection: log line + LED turns BLUE.
 */
#define WAKE_WORD_PHRASE               "HI ESP"

/**
 * How long (ms) MultiNet keeps listening for a command after the wake word
 * before timing out and returning to wake-word mode. Passed to
 * multinet->create() in detect_Task().
 */
#define MULTINET_COMMAND_TIMEOUT_MS    6000

/**
 * Minimum confidence probability (0.0 - 1.0) required to accept a detected
 * command. Detections below this are logged as "[IGNORED]" and dropped.
 */
#define MIN_COMMAND_CONFIDENCE         0.12f

/**
 * Voice command ID -> action map. IDs are registered in detect_Task() via
 * esp_mn_commands_add() and dispatched in speech_commands_action().
 *   ID 1 = "inverter on"  -> Relay 1 ON  (Relay 3 mirrors)
 *   ID 2 = "inverter off" -> Relay 1 OFF (Relay 3 mirrors)
 *   ID 3 = (reserved)     -> Relay 2 ON
 *   ID 4 = (reserved)     -> Relay 2 OFF
 * When adding phrases: register the ID in detect_Task() AND add a case in
 * speech_commands_action(), keeping both in sync.
 */
enum {
    VOICE_CMD_INVERTER_ON  = 1,   /**< "inverter on"  -> Relay 1 ON  */
    VOICE_CMD_INVERTER_OFF = 2,   /**< "inverter off" -> Relay 1 OFF */
    VOICE_CMD_RELAY2_ON    = 3,   /**< Relay 2 ON  */
    VOICE_CMD_RELAY2_OFF   = 4,   /**< Relay 2 OFF */
};
