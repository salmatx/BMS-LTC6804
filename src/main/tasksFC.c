/// This module creates and manages real-time processing tasks. All tasks created within this module are called
/// "Fast Core" tasks and run on Core 1 of the ESP32. Fast Core tasks perform real-time reading of BMS samples.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "watchdog.h"
#include "bms_adapter.h"
#include "bms_data.h"
#include "intercore_comm.h"
#include "logging.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "TASKS_FC"

/// TWDT feeding period in milliseconds. Used by Fast Core feeder.
#define WDT_FEED_MS             20

/// Fast Core real-time processing task period in milliseconds.
#define FAST_CORE_PERIOD_MS     50

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/


/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static void fast_core_task();
static void fast_core_feeder_task();

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/


/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Flag indicating whether feeding of HW TWDT is allowed. Set to false on Slow Core SW watchdog timeout.
static volatile bool s_allow_feeding = true;

/// Flag to signal tasks to exit gracefully
static volatile bool s_should_exit = false;

/// Main task handle for Fast Core tasks
static TaskHandle_t s_fast_core_task_handle = NULL;

/// TWDT task handle for Fast Core tasks
static TaskHandle_t s_fast_core_feeder_handle = NULL;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function creates all application Fast Core tasks.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t fast_core_tasks_create(void)
{
    BaseType_t result;

    // Create Fast Core processing task. Higher priority than Fast Core feeder to ensure real-time processing.
    result = xTaskCreatePinnedToCore(fast_core_task, "fast_core_task", 4096, NULL, 7, &s_fast_core_task_handle, 1);
    if (result != pdPASS) {
        BMS_LOGE("Failed to create Fast Core processing task");
        return ESP_FAIL;
    }

    // Create Fast Core TWDT feeder task. Lower priority than Fast Core processing to ensure processing runs.
    result = xTaskCreatePinnedToCore(fast_core_feeder_task, "fast_core_feeder_task", 2048, NULL, 6, &s_fast_core_feeder_handle, 1);
    if (result != pdPASS) {
        BMS_LOGE("Failed to create Fast Core feeder task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/// This function deletes all Fast Core tasks. Used before entering CONFIG state.
/// Tasks will unregister from TWDT gracefully before deletion.
///
/// \param None
/// \return None
void fast_core_tasks_delete(void)
{
    BMS_LOGI("Signaling Fast Core tasks to exit gracefully");
    s_should_exit = true;
    
    // Wait for tasks to exit gracefully (up to 500ms)
    for (int i = 0; i < 10; i++) {
        if (!s_fast_core_task_handle && !s_fast_core_feeder_handle) {
            break;  // Both tasks exited
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Force delete if still running (shouldn't happen)
    if (s_fast_core_feeder_handle) {
        BMS_LOGW("Force deleting Fast Core feeder task (didn't exit gracefully)");
        vTaskDelete(s_fast_core_feeder_handle);
        s_fast_core_feeder_handle = NULL;
    }
    
    if (s_fast_core_task_handle) {
        BMS_LOGW("Force deleting Fast Core processing task (didn't exit gracefully)");
        vTaskDelete(s_fast_core_task_handle);
        s_fast_core_task_handle = NULL;
    }
    
    // Reset flags for potential restart
    s_allow_feeding = true;
    s_should_exit = false;
    
    BMS_LOGI("Fast Core tasks cleaned up");
}


/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// Fast Core task reads BMS samples from BMS adapter and pushes them into inter-core queue for Slow Core (Core 0)
/// processing. Fast Core task runs in real-time and must complete its work within defined period, otherwise it
/// disables feeding of the hardware TWDT, allowing it to expire and reset the system.
///
/// \param None
/// \return None
static void fast_core_task()
{
    const bms_adapter_t *bms = bms_get_adapter();
    if (!bms) {
        BMS_LOGE("No BMS adapter selected");
        vTaskDelete(NULL);
    }

    // Waiting period for real-time loop
    const TickType_t period = pdMS_TO_TICKS(FAST_CORE_PERIOD_MS);
    // Variable handling previous wake time for accurate periodic delay
    TickType_t last_wake = xTaskGetTickCount();
    // Timing variables for checking real-time overrun
    TickType_t start;
    TickType_t end;

    bms_sample_t sample;

    // Main Fast Core loop
    while (!s_should_exit)
    {
        // Start timing for real-time overrun check
        start = xTaskGetTickCount();

        // Check free slots in inter-core queue. If none, disable feeding of HW TWDT.
        if (bms_queue_free_slots() == 0) {
            BMS_LOGE("BMS queue full (no free slots), stopping feeders and core1");
            s_allow_feeding = false;
        }

        /* Get one demo BMS sample */
        // Read one sample from BMS adapter
        esp_err_t err = bms->read_sample(&sample);
        // On success, push sample into inter-core queue
        if (err == ESP_OK) {
            if (!bms_queue_push(&sample)) {
                //On next iteration bms_queue_free_slots()==0 will trip and stop tasks
                BMS_LOGE("Failed to enqueue BMS sample (queue full or error)");
            }
        } else {
            BMS_LOGE("BMS read_sample failed: %s", esp_err_to_name(err));
        }

        // End timing and check for real-time overrun
        end = xTaskGetTickCount();
        // If real-time period was exceeded, disable feeding of HW TWDT
        if ((end - start) > period) {
            BMS_LOGW("Fast Core RT overrun: %lu ms > %d ms",
                     (unsigned long)pdTICKS_TO_MS(end - start), FAST_CORE_PERIOD_MS);
            s_allow_feeding = false;
        }

        // Puts task into blocked state for absolute period until next cycle (ensures real-time periodicity 20 Hz)
        vTaskDelayUntil(&last_wake, period);
    }
    
    BMS_LOGI("Fast Core processing task exiting gracefully");
    s_fast_core_task_handle = NULL;
    vTaskDelete(NULL);
}



/// Fast Core TWDT feeder task. This task periodically feeds (resets) the hardware TWDT to prevent timeout.
/// Feeding is only performed if \ref s_allow_feeding flag is true, otherwise feeding is skipped, allowing TWDT to expire
/// and reset the system.
///
/// \param None
/// \return None
static void fast_core_feeder_task()
{
    // Register current task with TWDT. If registration fails, delete task.
    if (bms_wdt_register_current_task() != ESP_OK) {
        BMS_LOGE("Failed to register Fast Core feeder to TWDT");
        vTaskDelete(NULL);
    }

    const TickType_t feed_ticks = pdMS_TO_TICKS(WDT_FEED_MS);

    // Main Fast Core feeder loop. Periodically feed TWDT if allowed.
    while (!s_should_exit)
    {
        // Variable_allow_feeding indicates whether feeding is allowed. Variable can be set to false
        // by Fast Core tasks on error conditions.
        if (s_allow_feeding) {
            if (bms_wdt_feed_self() != ESP_OK) {
                BMS_LOGE("HW WD feed failed (Fast Core feeder)");
            }
        }
        // Put task into blocked state for defined period
        vTaskDelay(feed_ticks);
    }
    
    // Unregister from TWDT before exiting to prevent watchdog trigger
    BMS_LOGI("Fast Core feeder unregistering from TWDT and exiting gracefully");
    bms_wdt_unregister_current_task();
    s_fast_core_feeder_handle = NULL;
    vTaskDelete(NULL);
}
