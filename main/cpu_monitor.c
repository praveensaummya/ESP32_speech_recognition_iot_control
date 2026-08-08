#include "cpu_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "CPU_MONITOR";
static TaskHandle_t s_monitor_task_handle = NULL;
static volatile bool s_monitor_enabled = false;

static void cpu_monitor_task(void *pvParameters)
{
    // Allocate buffer for task statistics
    char *stats_buffer = malloc(2048);
    if (stats_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for CPU stats");
        s_monitor_enabled = false;
        s_monitor_task_handle = NULL;
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "CPU Monitoring Enabled");

    while (s_monitor_enabled) {
        // Sample every 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));

        if (!s_monitor_enabled) {
            break;
        }

        printf("\n==================== CPU & TASK STATS ====================\n");
        printf("Task Name       Run Time (us)    %% CPU Usage\n");
        printf("----------------------------------------------------------\n");

        vTaskGetRunTimeStats(stats_buffer);
        printf("%s", stats_buffer);

        printf("==========================================================\n");
    }

    // Clean up memory on task exit
    free(stats_buffer);
    ESP_LOGI(TAG, "CPU Monitoring Disabled");
    s_monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

void cpu_monitor_start(void)
{
    if (s_monitor_enabled || s_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "CPU Monitor is already running.");
        return;
    }

    s_monitor_enabled = true;
    xTaskCreatePinnedToCore(
        cpu_monitor_task,
        "cpu_monitor",
        4 * 1024,
        NULL,
        1,
        &s_monitor_task_handle,
        0
    );
}

void cpu_monitor_stop(void)
{
    if (!s_monitor_enabled) {
        ESP_LOGW(TAG, "CPU Monitor is already stopped.");
        return;
    }

    // Setting this flag breaks the loop inside cpu_monitor_task and safely cleans up
    s_monitor_enabled = false;
}

void cpu_monitor_toggle(void)
{
    if (s_monitor_enabled) {
        cpu_monitor_stop();
    } else {
        cpu_monitor_start();
    }
}

bool cpu_monitor_is_running(void)
{
    return s_monitor_enabled;
}