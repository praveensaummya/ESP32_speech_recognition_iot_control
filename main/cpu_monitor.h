#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the CPU monitoring background task.
 */
void cpu_monitor_start(void);

/**
 * @brief Stops the CPU monitoring task and frees allocated memory.
 */
void cpu_monitor_stop(void);

/**
 * @brief Toggles the CPU monitoring task ON or OFF.
 */
void cpu_monitor_toggle(void);

/**
 * @brief Checks if CPU monitoring is currently active.
 * @return true if running, false if stopped.
 */
bool cpu_monitor_is_running(void);

#ifdef __cplusplus
}
#endif