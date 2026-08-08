
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_board_init.h"
#include "speech_commands_action.h"
#include "model_path.h"
#include "esp_process_sdkconfig.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "wifi_manager.h"


#include "cpu_monitor.h"

#define ENABLE_AUDIO_METER 0 //set to 0 disable, 1 to enable

/* @brief tag used for ESP serial console messages */
static const char TAG[] = "main";

/**
 * @brief this is an exemple of a callback that you can setup in your own app to get notified of wifi manager event.
 */
void cb_connection_ok(void *pvParameter){
	ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;

	/* transform IP to human readable string */
	char str_ip[16];
	esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

	ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);
}


int wakeup_flag = 0;
static const esp_afe_sr_iface_t *afe_handle = NULL;
static volatile int task_flag = 0;
srmodel_list_t *models = NULL;

static void print_audio_stream_status(afe_fetch_result_t *res, int afe_chunksize, int wakeup_flag)
{
    static uint32_t frame_count = 0;
    frame_count++;

    // Check VAD (Voice Activity Detection) state change
    static int prev_vad_state = -1;
    int vad_state = res->vad_state;
    bool vad_changed = (vad_state != prev_vad_state);
    prev_vad_state = vad_state;

    // Print status roughly every ~240ms (15 frames) or immediately when voice activity status changes
    if (frame_count % 10 != 0 && !vad_changed) {
        return;
    }

    // Calculate RMS energy & peak level from PCM 16-bit audio samples
    int16_t *samples = (int16_t *)res->data;
    int num_samples = res->data_size / sizeof(int16_t);
    int64_t sum_sq = 0;
    int16_t peak = 0;
    for (int i = 0; i < num_samples; i++) {
        int16_t s = samples[i];
        if (abs(s) > peak) {
            peak = abs(s);
        }
        sum_sq += (int32_t)s * s;
    }
    float rms = (num_samples > 0) ? sqrtf((float)sum_sq / num_samples) : 0.0f;

    // Generate terminal VU meter bar [====------]
    int vol_pct = (int)((rms / 4000.0f) * 100.0f);
    if (vol_pct > 100) vol_pct = 100;

    int bar_len = 10;
    int filled = (vol_pct * bar_len) / 100;
    char vu_bar[16];
    int idx = 0;
    vu_bar[idx++] = '[';
    for (int i = 0; i < bar_len; i++) {
        if (i < filled) {
            vu_bar[idx++] = '=';
        } else {
            vu_bar[idx++] = '-';
        }
    }
    vu_bar[idx++] = ']';
    vu_bar[idx] = '\0';

    const char *state_str = (wakeup_flag == 1) ? "\033[32m[LISTENING FOR COMMANDS]\033[0m" : "\033[33m[AWAITS WAKEWORD]\033[0m";
    const char *vad_str = (vad_state == AFE_VAD_SPEECH) ? "\033[31mSPEECH DETECTED\033[0m" : "SILENCE        ";

    printf("AUDIO STREAM | Level: %-12s (RMS:%4.0f, Peak:%5d) | VAD: %s | %s\n",
           vu_bar, rms, peak, vad_str, state_str);
    fflush(stdout);
}

void feed_Task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();
    assert(nch == feed_channel);
    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * feed_channel);
    assert(i2s_buff);

    while (task_flag) {
        esp_get_feed_data(true, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);

        afe_handle->feed(afe_data, i2s_buff);
    }
    if (i2s_buff) {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    esp_task_wdt_add(NULL);

    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);

    printf("multinet:%s\n", mn_name);
    fflush(stdout);

    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 6000);
    int mu_chunksize = multinet->get_samp_chunksize(model_data);

    assert(mu_chunksize == afe_chunksize);

    esp_mn_commands_clear();
    esp_mn_commands_add(1, "inverter on");
    esp_mn_commands_add(2, "inverter off");
    esp_mn_commands_update();

    multinet->print_active_speech_commands(model_data);
    fflush(stdout);

    printf("------------detect start------------\n");
    fflush(stdout);

    while (task_flag) {
        esp_task_wdt_reset(NULL);

        afe_fetch_result_t* res = afe_handle->fetch(afe_data); 
        if (!res || res->ret_value == ESP_FAIL) {
            printf("fetch error!\n");
            break;
        }
        
        #if ENABLE_AUDIO_METER
        print_audio_stream_status(res, afe_chunksize, wakeup_flag);
        #endif

        // 1. WAKEWORD DETECTED
        if (res->wakeup_state == WAKENET_DETECTED) {
            printf("WAKEWORD DETECTED: HI ESP\n");
            fflush(stdout);

            afe_handle->disable_wakenet(afe_data);
            wake_up_action();
            

            wakeup_flag = 1;
            continue;
        }

        // 2. COMMAND DETECTION & TIMEOUT HANDLER
        if (wakeup_flag == 1) {
            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);

            if (mn_state == ESP_MN_STATE_DETECTING) {
                continue;
            }

            // Command Successfully Detected
            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                for (int i = 0; i < mn_result->num; i++) {
                    printf("TOP %d, command_id: %d, phrase_id: %d, string: %s, prob: %f\n", 
                        i+1, mn_result->command_id[i], mn_result->phrase_id[i], mn_result->string, mn_result->prob[i]);
                    fflush(stdout);
                }

                if (mn_result->num > 0) {
                    if (mn_result->prob[0] >= 0.12f) {
                        speech_commands_action(mn_result->command_id[0]);
                    } else {
                        printf("[IGNORED] Low confidence detection (prob:%f < 0.12)\n", mn_result->prob[0]);
                    }
                }

                // Reset state back to Wakeword listening mode
                afe_handle->enable_wakenet(afe_data);
                wakeup_flag = 0;

                // Reset LED back to idle / green state
                led_set_off();

                printf("\n-----------awaits to be waken up-----------\n");
                fflush(stdout);
                continue;
            }

            // Timeout Occurred
            if (mn_state == ESP_MN_STATE_TIMEOUT) {
                esp_mn_results_t *mn_result = multinet->get_results(model_data);
                printf("timeout, string:%s\n", mn_result->string);
                fflush(stdout);

                // Reset state back to Wakeword listening mode
                afe_handle->enable_wakenet(afe_data);
                wakeup_flag = 0;

                // Ensure LED returns to idle state
                led_set_off(); 

                printf("\n-----------awaits to be waken up-----------\n");
                fflush(stdout);
                continue;
            }
        }
    }

    if (model_data) {
        multinet->destroy(model_data);
        model_data = NULL;
    }
    printf("detect exit\n");
    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // Initialize esp_netif (required for Wi-Fi in IDF v5.x)
    ESP_ERROR_CHECK(esp_netif_init());
    
    esp_err_t err = esp_event_loop_create_default();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE){
        ESP_ERROR_CHECK(err);
    }
    // Start Wi-Fi Manager AFTER NVS and netif are ready
    wifi_manager_start();

    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
    //voice recognition models
    models = esp_srmodel_init("model"); // partition label defined in partitions.csv
    ESP_ERROR_CHECK(esp_board_init(16000, 2, 16));

    led_init(); // Initialize built-in WS2812 Pixel LED
    led_set_off();

    // ESP_ERROR_CHECK(esp_sdcard_init("/sdcard", 10));


    afe_config_t *afe_config = afe_config_init(esp_get_input_format(), models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    afe_config->afe_ringbuf_size = 100;

    afe_handle = esp_afe_handle_from_config(afe_config);
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);

    afe_config_free(afe_config);


    task_flag = 1;
    xTaskCreatePinnedToCore(
        &detect_Task, 
        "detect_Task", 
        8 * 1024, 
        (void*)afe_data, 5,
         NULL, 1);
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void*)afe_data, 5, NULL, 0);

    cpu_monitor_start();
}

