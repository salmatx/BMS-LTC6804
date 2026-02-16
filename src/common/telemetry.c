/// This module implements system telemetry collection for ESP32 and BMS hardware.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "telemetry.h"
#include "logging.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "TELEMETRY"

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static uint8_t get_cpu_load(void);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Cached device unique ID (based on MAC address)
static char s_device_id[18] = {0};
/// Cached software version
static char s_sw_version[32] = {0};
/// Previous total runtime for delta calculation
static uint32_t s_last_total_runtime = 0;
/// Previous idle runtime for delta calculation
static uint32_t s_last_idle_runtime = 0;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// Initialize telemetry module. Caches device ID and software version.
///
/// \param None
/// \return None
void telemetry_init(void)
{
    // Get and cache device ID (MAC address)
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err == ESP_OK) {
        snprintf(s_device_id, sizeof(s_device_id), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(s_device_id, sizeof(s_device_id), "UNKNOWN");
        BMS_LOGW("Failed to read MAC address: %s", esp_err_to_name(err));
    }
    
    // Get and cache software version
    const esp_app_desc_t *app_desc = esp_app_get_description();
    snprintf(s_sw_version, sizeof(s_sw_version), "%s", app_desc->version);
    
    #if (configGENERATE_RUN_TIME_STATS != 1)
    BMS_LOGW("FreeRTOS runtime stats not enabled - CPU load will be 0");
    BMS_LOGW("Enable CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS in menuconfig");
    #endif
    
    BMS_LOGI("Telemetry initialized: ID=%s, SW=%s", s_device_id, s_sw_version);
}

/// Get device unique ID (MAC address as string).
///
/// \param id_buf Buffer to store device ID
/// \param buf_size Size of buffer
/// \return None
void telemetry_get_device_id(char *id_buf, size_t buf_size)
{
    if (id_buf && buf_size > 0) {
        snprintf(id_buf, buf_size, "%s", s_device_id);
    }
}

/// Get software version string.
///
/// \param ver_buf Buffer to store software version
/// \param buf_size Size of buffer
/// \return None
void telemetry_get_sw_version(char *ver_buf, size_t buf_size)
{
    if (ver_buf && buf_size > 0) {
        snprintf(ver_buf, buf_size, "%s", s_sw_version);
    }
}

/// Collect ESP32 system telemetry.
///
/// \param telem Pointer to telemetry structure to fill
/// \return None
void telemetry_get_esp32_telemetry(esp32_telemetry_t *telem)
{
    if (!telem) return;
    
    // Copy cached device ID and version
    snprintf(telem->device_id, sizeof(telem->device_id), "%s", s_device_id);
    snprintf(telem->sw_version, sizeof(telem->sw_version), "%s", s_sw_version);
    
    // Get system measurements
    telem->cpu_load = get_cpu_load();
    telem->free_heap = esp_get_free_heap_size();
    telem->min_free_heap = esp_get_minimum_free_heap_size();
}

/// Get LTC6804 status registers. This is a placeholder - implement actual register reading.
///
/// \param status Pointer to status structure to fill
/// \return None
void telemetry_get_ltc6804_status(ltc6804_status_t *status)
{
    if (!status) return;
    
    // TODO: Implement actual LTC6804 status register reading
    // For now, return placeholder values
    status->status_a = 0;
    status->status_b = 0;
    status->valid = false;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// Uses delta calculation over time for accurate measurement.
///
/// \param None
/// \return CPU load percentage (0-100)
static uint8_t get_cpu_load(void)
{
    #if (configGENERATE_RUN_TIME_STATS == 1)
    // Get number of tasks
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    if (num_tasks == 0) return 0;
    
    // Allocate array for task status
    TaskStatus_t *task_array = pvPortMalloc(num_tasks * sizeof(TaskStatus_t));
    if (!task_array) return 0;
    
    // Get runtime stats for all tasks
    uint32_t total_runtime;
    num_tasks = uxTaskGetSystemState(task_array, num_tasks, &total_runtime);
    
    // Find idle task and get its runtime
    uint32_t idle_runtime = 0;
    for (UBaseType_t i = 0; i < num_tasks; i++) {
        // Idle task has priority 0
        if (task_array[i].uxCurrentPriority == 0) {
            idle_runtime += task_array[i].ulRunTimeCounter;
        }
    }
    
    vPortFree(task_array);
    
    // Calculate CPU load using delta since last measurement
    uint8_t load = 0;
    if (s_last_total_runtime > 0) {
        uint32_t delta_total = total_runtime - s_last_total_runtime;
        uint32_t delta_idle = idle_runtime - s_last_idle_runtime;
        
        if (delta_total > 0) {
            uint32_t idle_percent = (delta_idle * 100) / delta_total;
            load = (idle_percent > 100) ? 0 : (100 - idle_percent);
        }
    }
    
    // Store current values for next calculation
    s_last_total_runtime = total_runtime;
    s_last_idle_runtime = idle_runtime;
    
    return load;
    #else
    // Runtime stats not enabled
    return 0;
    #endif
}
