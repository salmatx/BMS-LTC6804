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
#include <stdio.h>

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
/// Cached LTC6804 status (updated from Core 1, read from Core 0)
static ltc6804_status_t s_ltc_status = {0};
/// Spinlock for protecting LTC6804 status access across cores
static portMUX_TYPE s_ltc_status_lock = portMUX_INITIALIZER_UNLOCKED;
/// Cached reset message (populated once at boot)
static char s_reset_msg[RESET_MSG_MAXLEN] = {0};

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// Function initializes telemetry module. It caches device ID and software version.
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
    
    // If last reset was caused by Task Watchdog, retrieve last 5 error log entries from RTC memory.
    // For other reset reasons, s_reset_msg stays empty
    if (esp_reset_reason() == ESP_RST_TASK_WDT) {
        char entries[BMS_LOG_ENTRY_COUNT][BMS_LOG_ENTRY_MAXLEN];
        int entry_count = 0;
        bms_log_rtc_get_entries(entries, &entry_count);

        size_t off = 0;
        for (int i = 0; i < entry_count; i++) {
            int written = snprintf(s_reset_msg + off, sizeof(s_reset_msg) - off,
                                   "%s%s", (i > 0) ? " | " : "", entries[i]);
            if (written < 0 || off + (size_t)written >= sizeof(s_reset_msg)) break;
            off += (size_t)written;
        }
    }

    // Clear RTC log buffer after reading so next boot starts fresh
    bms_log_rtc_clear();

    BMS_LOGI("Telemetry initialized: ID=%s, SW=%s", s_device_id, s_sw_version);

    return;
}

/// Function gets device unique ID (MAC address as string).
///
/// \param[out] id_buf Buffer to store device ID
/// \param[in] buf_size Size of buffer
/// \return None
void telemetry_get_device_id(char *id_buf, size_t buf_size)
{
    if (id_buf && buf_size > 0) {
        snprintf(id_buf, buf_size, "%s", s_device_id);
    }

    return;
}

/// Function gets software version string.
///
/// \param[out] ver_buf Buffer to store software version
/// \param[in] buf_size Size of buffer
/// \return None
void telemetry_get_sw_version(char *ver_buf, size_t buf_size)
{
    if (ver_buf && buf_size > 0) {
        snprintf(ver_buf, buf_size, "%s", s_sw_version);
    }

    return;
}

/// Function collects ESP32 system telemetry.
///
/// \param[out] telem Pointer to telemetry structure to fill
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
    telem->reset_reason = (uint8_t)esp_reset_reason();
    snprintf(telem->reset_msg, sizeof(telem->reset_msg), "%s", s_reset_msg);

    return;
}

/// Function gets LTC6804 status registers. Returns cached status updated by telemetry_update_ltc6804_status().
///
/// \param[out] status Pointer to status structure to fill
/// \return None
void telemetry_get_ltc6804_status(ltc6804_status_t *status)
{
    if (!status) return;

    taskENTER_CRITICAL(&s_ltc_status_lock);
    *status = s_ltc_status;
    taskEXIT_CRITICAL(&s_ltc_status_lock);

    return;
}

/// Function updates cached LTC6804 status. Intended to be called from Core 1 after reading status registers.
/// Parses raw register bytes into the ::ltc6804_status_t fields.
///
/// \param[in] stata Raw 6 bytes from Status Register Group A (SOC, ITMP, VA)
/// \param[in] statb Raw 6 bytes from Status Register Group B (VD, cell flags, diag)
/// \param[in] valid True if the status registers were read successfully
/// \return None
void telemetry_update_ltc6804_status(const uint8_t stata[6], const uint8_t statb[6], bool valid)
{
    taskENTER_CRITICAL(&s_ltc_status_lock);
    if (valid) {
        // STATA: SOC (bytes 0-1), ITMP (bytes 2-3), VA (bytes 4-5) — all little-endian
        s_ltc_status.soc  = stata[0] | ((uint16_t)stata[1] << 8);
        s_ltc_status.itmp = stata[2] | ((uint16_t)stata[3] << 8);
        s_ltc_status.va   = stata[4] | ((uint16_t)stata[5] << 8);
        // STATB: VD (bytes 0-1), cell flags (bytes 2-4), diag (byte 5) — all little-endian
        s_ltc_status.vd         = statb[0] | ((uint16_t)statb[1] << 8);
        s_ltc_status.cell_flags = statb[2] | ((uint32_t)statb[3] << 8) | ((uint32_t)statb[4] << 16);
        s_ltc_status.diag       = statb[5];
    }
    s_ltc_status.valid = valid;
    taskEXIT_CRITICAL(&s_ltc_status_lock);

    return;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// Uses delta calculation over time for accurate measurement.
/// Estimate current CPU load from FreeRTOS run-time statistics.
///
/// This helper computes load over the interval since the previous call using
/// delta counters from ::uxTaskGetSystemState: `load = 100 - idle_percent`.
/// Idle time is obtained by summing run-time counters of tasks at priority 0.
///
/// \param None
/// \return CPU load in percent (0-100) for the last sampling interval, or 0 when
///         measurement cannot be computed (first call, allocation failure, no tasks,
///         or run-time stats disabled)
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
