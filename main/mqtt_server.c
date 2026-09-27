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

#include "app_config.h" // Central documented config: NVS keys, MQTT topics, QoS

// ---------------------------------------------------------------------------
// Local, git-ignored broker credentials (template: main/secrets.h.example).
// Each developer copies the template to main/secrets.h and fills in their
// own credentials; .gitignore keeps that file out of version control.
// A fresh clone WITHOUT secrets.h still compiles: placeholder creds are
// used and MQTT stays disconnected until real credentials are saved to the
// device with POST /api/config/mqtt (persisted to NVS).
// ---------------------------------------------------------------------------
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "main/secrets.h missing - copy main/secrets.h.example and add your broker credentials (MQTT will not connect until configured via POST /api/config/mqtt)."
#define MQTT_DEFAULT_BROKER_URI    "mqtts://CHANGE-ME.example.invalid"
#define MQTT_DEFAULT_BROKER_USER   "CHANGE-ME"
#define MQTT_DEFAULT_BROKER_PASS   "CHANGE-ME"
#endif

/**
 * @file mqtt_server.c
 * @brief MQTT cloud client + REST API routes (relay control, MQTT config,
 *        voice-recognition toggle, reachability ping).
 *
 * Broker settings live in NVS namespace NVS_NAMESPACE_MQTT; the values in
 * secrets.h are only compile-time fallbacks used when NVS has no entry.
 */
static const char *TAG = "MQTT_CLIENT";
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

static char s_broker_uri[256];
static char s_broker_user[64];
static char s_broker_pass[64];

// Global Voice Recognition State Flag
bool g_voice_recognition_enabled = true;

// Helper to set CORS Headers for Mobile App requests
static void set_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

// Global OPTIONS Preflight Handler for CORS
static esp_err_t options_handler(httpd_req_t *req) {
    set_cors_headers(req);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ==========================================
// NVS Storage Functions (MQTT & Voice)
// ==========================================

void load_voice_recognition_state(void)
{
    nvs_handle_t nvs_h;
    uint8_t enabled = 1; // Default to ON (1)
    if (nvs_open(NVS_NAMESPACE_VOICE, NVS_READONLY, &nvs_h) == ESP_OK) {
        if (nvs_get_u8(nvs_h, NVS_KEY_VOICE_ENABLED, &enabled) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded Voice Recognition State from NVS: %s", enabled ? "ON" : "OFF");
        }
        nvs_close(nvs_h);
    } else {
        ESP_LOGW(TAG, "No Voice NVS state found, defaulting Voice Recognition to ON");
    }
    g_voice_recognition_enabled = (enabled != 0);
}

void save_voice_recognition_state(bool enabled)
{
    nvs_handle_t nvs_h;
    if (nvs_open(NVS_NAMESPACE_VOICE, NVS_READWRITE, &nvs_h) == ESP_OK) {
        nvs_set_u8(nvs_h, NVS_KEY_VOICE_ENABLED, enabled ? 1 : 0);
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
        ESP_LOGI(TAG, "Saved Voice Recognition State to NVS: %s", enabled ? "ON" : "OFF");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS namespace for Voice Config");
    }
}

esp_err_t mqtt_get_config(char *uri_buf, size_t uri_len, 
                        char *user_buf, size_t user_len, 
                        char *pass_buf, size_t pass_len)
{
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_MQTT, NVS_READONLY, &nvs_h);
    if (err != ESP_OK) {
        snprintf(uri_buf, uri_len, "%s", MQTT_DEFAULT_BROKER_URI);
        snprintf(user_buf, user_len, "%s", MQTT_DEFAULT_BROKER_USER);
        snprintf(pass_buf, pass_len, "%s", MQTT_DEFAULT_BROKER_PASS);
        return ESP_OK;
    }

    size_t req_len = uri_len;
    if (nvs_get_str(nvs_h, NVS_KEY_MQTT_URI, uri_buf, &req_len) != ESP_OK || strlen(uri_buf) == 0) {
        snprintf(uri_buf, uri_len, "%s", MQTT_DEFAULT_BROKER_URI);
    }

    req_len = user_len;
    if (nvs_get_str(nvs_h, NVS_KEY_MQTT_USER, user_buf, &req_len) != ESP_OK) {
        snprintf(user_buf, user_len, "%s", MQTT_DEFAULT_BROKER_USER);
    }

    req_len = pass_len;
    if (nvs_get_str(nvs_h, NVS_KEY_MQTT_PASS, pass_buf, &req_len) != ESP_OK) {
        snprintf(pass_buf, pass_len, "%s", MQTT_DEFAULT_BROKER_PASS);
    }

    nvs_close(nvs_h);
    return ESP_OK;
}

esp_err_t mqtt_save_config(const char *uri, const char *user, const char *pass)
{
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE_MQTT, NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    if (uri)  nvs_set_str(nvs_h, NVS_KEY_MQTT_URI, uri);
    if (user) nvs_set_str(nvs_h, NVS_KEY_MQTT_USER, user);
    if (pass) nvs_set_str(nvs_h, NVS_KEY_MQTT_PASS, pass);

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
    esp_err_t err = nvs_open(NVS_NAMESPACE_MQTT, NVS_READWRITE, &nvs_h);
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
        esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_RELAY_STATUS, status_json, 0, MQTT_QOS, 0);
        ESP_LOGI(TAG, "Published status: %s", status_json);
    }
}

void mqtt_publish_voice_status(bool enabled)
{
    if (s_mqtt_client != NULL) {
        char status_json[64];
        snprintf(status_json, sizeof(status_json), "{\"voice_enabled\":%s}", enabled ? "true" : "false");
        esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_VOICE_STATUS, status_json, 0, MQTT_QOS, 0);
        ESP_LOGI(TAG, "Published Voice Status to MQTT: %s", status_json);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to MQTT Cloud Broker!");
            esp_mqtt_client_subscribe(event->client, MQTT_TOPIC_RELAY_COMMAND, MQTT_QOS);
            esp_mqtt_client_subscribe(event->client, MQTT_TOPIC_VOICE_COMMAND, MQTT_QOS);
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
                cJSON *voice = cJSON_GetObjectItem(root, "voice_enabled");

                if (cJSON_IsNumber(relay) && cJSON_IsNumber(state)) {
                    set_relay_state(relay->valueint, state->valueint);
                } else if(voice != NULL){
                    bool new_state = cJSON_IsTrue(voice) || (cJSON_IsNumber(voice) && voice->valueint == 1);
                    g_voice_recognition_enabled = new_state;
                    save_voice_recognition_state(new_state);     // Save to NVS
                    mqtt_publish_voice_status(new_state);        // Sync back to cloud
                    
                    ESP_LOGI(TAG, "Voice Recognition changed via MQTT to: %s", new_state ? "ON" : "OFF");
                } else {
                    ESP_LOGE(TAG, "Invalid JSON structure.");
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
    set_cors_headers(req);
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
    buf[cur_len] = '\0';

    ESP_LOGI(TAG, "Received MQTT Config JSON: %s", buf);

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

    mqtt_save_config(uri, user, pass);
    cJSON_Delete(root);

    mqtt_app_start();

    const char *resp_str = "{\"status\":\"ok\",\"message\":\"MQTT configuration saved and client restarted successfully!\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t mqtt_config_delete_handler(httpd_req_t *req)
{
    set_cors_headers(req);
    mqtt_clear_config();
    mqtt_app_stop();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"MQTT configuration erased and client stopped!\"}");

    return ESP_OK;
}

static esp_err_t relay_post_handler(httpd_req_t *req) {
    set_cors_headers(req);
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *relay = cJSON_GetObjectItem(root, "relay");
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsNumber(relay) && cJSON_IsNumber(state)) {
            set_relay_state(relay->valueint, state->valueint);
            mqtt_publish_relay_status(relay->valueint, state->valueint);
        }
        cJSON_Delete(root);
    }
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// ==========================================
// HTTP Handlers for Voice Recognition Toggle
// ==========================================

static esp_err_t voice_config_post_handler(httpd_req_t *req)
{
    set_cors_headers(req);
    char buf[128] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *enabled_item = cJSON_GetObjectItem(root, "enabled");
        if (cJSON_IsBool(enabled_item)) {
            bool new_state = cJSON_IsTrue(enabled_item);
            g_voice_recognition_enabled = new_state;
            save_voice_recognition_state(new_state);
            mqtt_publish_voice_status(new_state); // Sync state back to MQTT

            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
            cJSON_Delete(root);
            return ESP_OK;
        }
        cJSON_Delete(root);
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON payload");
    return ESP_FAIL;
}

static esp_err_t voice_status_get_handler(httpd_req_t *req)
{
    set_cors_headers(req);
    char resp_str[64];
    snprintf(resp_str, sizeof(resp_str), "{\"voice_enabled\":%s}", g_voice_recognition_enabled ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp_str);
    return ESP_OK;
}

// ==========================================
// Route Registration
// ==========================================

// GET /api/status - reachability ping used by the JCON mobile app
// (_checkDeviceReachability() calls http://<ip>:8080/api/status and expects HTTP 200.
//  Full app integration guide: docs/JCON_APP.md)
static esp_err_t status_get_handler(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close"); // Prevents ESP32 socket leak (matches app header)
    // The body advertises the API port so the app can pick up a changed
    // APP_HTTP_API_PORT without a firmware-specific update.
    char resp_str[96];
    snprintf(resp_str, sizeof(resp_str),
             "{\"status\":\"online\",\"device\":\"ESP32-S3 Inverter\",\"port\":%d}",
             APP_HTTP_API_PORT);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void register_mqtt_http_routes(httpd_handle_t server)
{
    if (server == NULL) {
        ESP_LOGE(TAG, "Cannot register routes: HTTP server handle is NULL");
        return;
    }

    // Load Voice Recognition State from NVS on server startup
    load_voice_recognition_state();

    // 1. OPTIONS Preflight Handler for CORS
    httpd_uri_t options_uri = {
        .uri      = "/api/*",
        .method   = HTTP_OPTIONS,
        .handler  = options_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &options_uri);

    // 2. POST /api/config/mqtt
    httpd_uri_t post_uri = {
        .uri      = "/api/config/mqtt",
        .method   = HTTP_POST,
        .handler  = mqtt_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &post_uri);

    // 3. DELETE /api/config/mqtt
    httpd_uri_t delete_uri = {
        .uri      = "/api/config/mqtt",
        .method   = HTTP_DELETE,
        .handler  = mqtt_config_delete_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &delete_uri);

    // 4. POST /api/relay 
    httpd_uri_t post_relay_uri = {
        .uri      = "/api/relay",
        .method   = HTTP_POST,
        .handler  = relay_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &post_relay_uri);

    // 5. POST /api/voice/config
    httpd_uri_t post_voice_uri = {
        .uri      = "/api/voice/config",
        .method   = HTTP_POST,
        .handler  = voice_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &post_voice_uri);

    // 6. GET /api/voice/status
    httpd_uri_t get_voice_uri = {
        .uri      = "/api/voice/status",
        .method   = HTTP_GET,
        .handler  = voice_status_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &get_voice_uri);

    // 7. GET /api/status - reachability ping for the mobile app (must return 200)
    httpd_uri_t get_status_uri = {
        .uri      = "/api/status",
        .method   = HTTP_GET,
        .handler  = status_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &get_status_uri);

    ESP_LOGI(TAG, "HTTP endpoints for /api/config/mqtt, /api/relay, /api/voice, and /api/status successfully registered.");
}