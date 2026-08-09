#include "mqtt_server.h"
#include "speech_commands_action.h" // For set_relay_state()
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"         
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

#define NVS_NAMESPACE       "mqtt_config"
#define NVS_KEY_URI         "broker_uri"
#define NVS_KEY_USER        "broker_user"
#define NVS_KEY_PASS        "broker_pass"

// Default fallback values
#define DEFAULT_BROKER_URI  ""
#define DEFAULT_BROKER_USER ""
#define DEFAULT_BROKER_PASS ""

static const char *TAG = "MQTT_CLIENT";
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

static char s_broker_uri[256];
static char s_broker_user[64];
static char s_broker_pass[64];

// ==========================================
// NVS Storage Functions
// ==========================================

esp_err_t mqtt_get_config(char *uri_buf, size_t uri_len, 
                        char *user_buf, size_t user_len, 
                        char *pass_buf, size_t pass_len)
{
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h);
    if (err != ESP_OK) {
        snprintf(uri_buf, uri_len, "%s", DEFAULT_BROKER_URI);
        snprintf(user_buf, user_len, "%s", DEFAULT_BROKER_USER);
        snprintf(pass_buf, pass_len, "%s", DEFAULT_BROKER_PASS);
        return ESP_OK;
    }

    // Load URI
    size_t req_len = uri_len;
    if (nvs_get_str(nvs_h, NVS_KEY_URI, uri_buf, &req_len) != ESP_OK || strlen(uri_buf) == 0) {
        snprintf(uri_buf, uri_len, "%s", DEFAULT_BROKER_URI);
    }

    // Load Username
    req_len = user_len;
    if (nvs_get_str(nvs_h, NVS_KEY_USER, user_buf, &req_len) != ESP_OK) {
        snprintf(user_buf, user_len, "%s", DEFAULT_BROKER_USER);
    }

    // Load Password
    req_len = pass_len;
    if (nvs_get_str(nvs_h, NVS_KEY_PASS, pass_buf, &req_len) != ESP_OK) {
        snprintf(pass_buf, pass_len, "%s", DEFAULT_BROKER_PASS);
    }

    nvs_close(nvs_h);
    return ESP_OK;
}

esp_err_t mqtt_save_config(const char *uri, const char *user, const char *pass)
{
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    if (uri)  nvs_set_str(nvs_h, NVS_KEY_URI, uri);
    if (user) nvs_set_str(nvs_h, NVS_KEY_USER, user);
    if (pass) nvs_set_str(nvs_h, NVS_KEY_PASS, pass);

    err = nvs_commit(nvs_h);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved MQTT Config (URI, Username, Password) to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to write config to NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_h);
    return err;
}

esp_err_t mqtt_clear_config(void)
{
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h);
    if (err == ESP_OK) {
        err = nvs_erase_all(nvs_h);
        if (err == ESP_OK) {
            nvs_commit(nvs_h);
            ESP_LOGI(TAG, "Erased all MQTT configurations from NVS");
        } else {
            ESP_LOGE(TAG, "Failed to erase NVS namespace: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_h);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS namespace to erase: %s", esp_err_to_name(err));
    }
    return err;
}

// ==========================================
// MQTT Client Core & Messaging
// ==========================================

void mqtt_publish_relay_status(int relay_id, int state)
{
    if (s_mqtt_client != NULL) {
        char status_json[64];
        snprintf(status_json, sizeof(status_json), "{\"relay\":%d,\"state\":%d}", relay_id, state ? 1 : 0);
        esp_mqtt_client_publish(s_mqtt_client, "device/relays/status", status_json, 0, 1, 0);
        ESP_LOGI(TAG, "Published status: %s", status_json);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT Cloud Broker!");
            esp_mqtt_client_subscribe(event->client, "device/relays/command", 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from MQTT Cloud Broker");
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT Command Received: %.*s", event->data_len, event->data);

            cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
            if (root != NULL) {
                cJSON *relay = cJSON_GetObjectItem(root, "relay");
                cJSON *state = cJSON_GetObjectItem(root, "state");

                if (cJSON_IsNumber(relay) && cJSON_IsNumber(state)) {
                    set_relay_state(relay->valueint, state->valueint);
                } else {
                    ESP_LOGE(TAG, "Invalid JSON structure. Expected: {\"relay\": int, \"state\": int}");
                }
                cJSON_Delete(root);
            } else {
                ESP_LOGE(TAG, "Failed to parse incoming JSON payload");
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Event Error occurred");
            break;

        default:
            break;
    }
}

void mqtt_app_stop(void)
{
    if (s_mqtt_client != NULL) {
        ESP_LOGI(TAG, "Stopping active MQTT client instance...");
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }
}

void mqtt_app_start(void)
{
    mqtt_app_stop();

    // Fetch stored settings from NVS
    mqtt_get_config(s_broker_uri, sizeof(s_broker_uri),
                    s_broker_user, sizeof(s_broker_user),
                    s_broker_pass, sizeof(s_broker_pass));

    if (strlen(s_broker_uri) == 0) {
        ESP_LOGW(TAG, "No MQTT Broker URI configured. Skipping MQTT client launch.");
        return;
    }

    ESP_LOGI(TAG, "Starting MQTT Client with URI: %s", s_broker_uri);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = s_broker_uri,
            .verification = {
                .crt_bundle_attach = esp_crt_bundle_attach,
            },
        },
        .credentials = {
            .username = (strlen(s_broker_user) > 0) ? s_broker_user : NULL,
            .authentication = {
                .password = (strlen(s_broker_pass) > 0) ? s_broker_pass : NULL,
            },
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

// ==========================================
// HTTP Handlers for Mobile App Re-configuration
// ==========================================

static esp_err_t mqtt_config_post_handler(httpd_req_t *req)
{
    char buf[512] = {0};
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;

    if (total_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive payload");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0'; // Ensure null-termination

    ESP_LOGI(TAG, "Received MQTT Config JSON: %s", buf);

    // Parse JSON payload
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    cJSON *uri_item  = cJSON_GetObjectItem(root, "uri");
    cJSON *user_item = cJSON_GetObjectItem(root, "username");
    cJSON *pass_item = cJSON_GetObjectItem(root, "password");

    if (!cJSON_IsString(uri_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'uri' field");
        return ESP_FAIL;
    }

    const char *uri  = uri_item->valuestring;
    const char *user = cJSON_IsString(user_item) ? user_item->valuestring : "";
    const char *pass = cJSON_IsString(pass_item) ? pass_item->valuestring : "";

    // Save configuration to NVS
    mqtt_save_config(uri, user, pass);
    cJSON_Delete(root);

    // Restart MQTT client with new configuration
    mqtt_app_start();

    // Send successful JSON response back to client
    const char *resp_str = "{\"status\":\"ok\",\"message\":\"MQTT configuration saved and client restarted successfully!\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK; // Crucial: returning ESP_OK tells esp_http_server the request succeeded
}

static esp_err_t mqtt_config_delete_handler(httpd_req_t *req)
{
    // 1. Wipe saved configuration from NVS
    mqtt_clear_config();

    // 2. Stop running MQTT client instance
    mqtt_app_stop();

    // 3. Respond with JSON success
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"MQTT configuration erased and client stopped!\"}");

    return ESP_OK;
}


static esp_err_t relay_post_handler(httpd_req_t *req) {
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *relay = cJSON_GetObjectItem(root, "relay");
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsNumber(relay) && cJSON_IsNumber(state)) {
            set_relay_state(relay->valueint, state->valueint);
            // Optionally publish status update to MQTT as well
            mqtt_publish_relay_status(relay->valueint, state->valueint);
        }
        cJSON_Delete(root);
    }
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static httpd_handle_t last_registered_server = NULL;

void register_mqtt_http_routes(httpd_handle_t server) 
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Cannot register routes: HTTP server handle is NULL");
        return;
    }

    // If this exact server instance already has routes registered, skip safely
    if (last_registered_server == server) {
        return;
    }

    // Register POST endpoint
    httpd_uri_t post_uri = {
        .uri      = "/api/config/mqtt",
        .method   = HTTP_POST,
        .handler  = mqtt_config_post_handler,
        .user_ctx = NULL
    };
    esp_err_t err_post = httpd_register_uri_handler(server, &post_uri);

    // Register DELETE endpoint
    httpd_uri_t delete_uri = {
        .uri      = "/api/config/mqtt",
        .method   = HTTP_DELETE,
        .handler  = mqtt_config_delete_handler,
        .user_ctx = NULL
    };
    esp_err_t err_del = httpd_register_uri_handler(server, &delete_uri);

    // 3. POST /api/relay  <-- ADD THIS NEW ROUTE
    httpd_uri_t post_relay_uri = {
        .uri      = "/api/relay",
        .method   = HTTP_POST,
        .handler  = relay_post_handler,
        .user_ctx = NULL
    };
    esp_err_t err_relay = httpd_register_uri_handler(server, &post_relay_uri);
    
    if ((err_post == ESP_OK || err_post == ESP_ERR_HTTPD_HANDLER_EXISTS) &&
        (err_del == ESP_OK || err_del == ESP_ERR_HTTPD_HANDLER_EXISTS)) {
        last_registered_server = server;
        ESP_LOGI(TAG, "HTTP endpoints for /api/config/mqtt are successfully registered.");
    } else {
        ESP_LOGE(TAG, "Failed to register HTTP endpoints: POST (%s), DELETE (%s)", 
                 esp_err_to_name(err_post), esp_err_to_name(err_del));
    }
}