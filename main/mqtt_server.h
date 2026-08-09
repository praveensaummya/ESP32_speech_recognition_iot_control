#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the MQTT client using the credentials and URI loaded from NVS.
 */
void mqtt_app_start(void);

/**
 * @brief Stops and cleans up the active MQTT client instance.
 */
void mqtt_app_stop(void);

/**
 * @brief Publishes current relay state to the MQTT Cloud broker.
 * 
 * @param relay_id 1 for Relay 1, 2 for Relay 2
 * @param state 1 for ON, 0 for OFF
 */
void mqtt_publish_relay_status(int relay_id, int state);

/**
 * @brief Saves new MQTT Broker URI, Username, and Password into NVS flash memory instantly.
 * 
 * @param uri The broker URL (e.g. "mqtts://your-hivemq-endpoint:8883")
 * @param user The broker username string
 * @param pass The broker password string
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_save_config(const char *uri, const char *user, const char *pass);

/**
 * @brief Retrieves the current MQTT configuration (URI, Username, Password) stored in NVS.
 * 
 * @param uri_buf Buffer to store the broker URI
 * @param uri_len Size of uri_buf
 * @param user_buf Buffer to store the username
 * @param user_len Size of user_buf
 * @param pass_buf Buffer to store the password
 * @param pass_len Size of pass_buf
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mqtt_get_config(char *uri_buf, size_t uri_len, 
                        char *user_buf, size_t user_len, 
                        char *pass_buf, size_t pass_len);

/**
 * @brief Registers the /api/config/mqtt HTTP POST endpoint on the local web server.
 * 
 * @param server Handle to active httpd_handle_t web server
 */
void register_mqtt_http_routes(httpd_handle_t server);

#ifdef __cplusplus
}
#endif