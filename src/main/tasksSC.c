/// This module creates and manages application tasks running on Slow Core including watchdog feeding.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "watchdog.h"
#include "tasksSC.h"
#include "appsm.h"
#include "logging.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "TASKS_SC"

/* Slow Core SW watchdog configuration (functional, not hard RT) */
/// Slow Core SW watchdog strobe period and timeout in miliseconds. Slow Core SW watchdog monitors duty cycle of Slow Core task.
/// If Slow Core task does not strobe within timeout period, HW TWDT is allowed to expire, causing system reset.
#define CORE0_SW_STROBE_MS    1000
/// Slow Core SW watchdog timeout in milliseconds.
#define CORE0_SW_TIMEOUT_MS  30000

/// TWDT feeding period in milliseconds. Used by Slow Core feeder.
#define WDT_FEED_MS             20

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static void slow_core_task();
static void slow_core_feeder_task();

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Flag indicating whether feeding of HW TWDT is allowed. Set to false on Slow Core SW watchdog timeout.
static volatile bool s_allow_feeding = true;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function creates application main task on Slow Core. Tasks handles are not used and thus not returned,
/// because it is not intended to manage tasks (suspend, delete etc,) after creation.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t slow_core_task_create(void)
{
    BaseType_t result;

    // Create Slow Core task. Lower priority than Slow Core feeder to ensure feeder runs.
    result = xTaskCreatePinnedToCore(slow_core_task, "slow_core_task", 4096, NULL, 4, NULL, 0);
    if (result != pdPASS) {
        BMS_LOGE("Failed to create Slow Core task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/// This function creates TWDT task on Slow Core. Tasks handles are not used and thus not returned,
/// because it is not intended to manage tasks (suspend, delete etc,) after creation.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t slow_core_TWDT_create(void)
{
    BaseType_t result;

    // Create Slow Core TWDT feeder task. Higher priority than Slow Core task to ensure feeder runs.
    result = xTaskCreatePinnedToCore(slow_core_feeder_task, "slow_core_feeder_task", 2048, NULL, 5, NULL, 0);
    if (result != pdPASS) {
        BMS_LOGE("Failed to create Slow Core feeder task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// Slow Core task reads BMS samples from inter-core queue, computes statistics, and publishes them via MQTT.
/// This task also monitors its own duty cycle using a software watchdog mechanism. If the task fails to complete
/// its work within the defined timeout period, it disables feeding of the hardware TWDT, allowing it to expire
/// and reset the system. Processing is gated so that samples are consumed only after MQTT publish
/// is acknowledged (QoS1 PUBACK).
///
/// \param arg Unused in current implementation and may be NULL
/// \return None
static void slow_core_task()
{
    // SW watchdog timing parameters
    const TickType_t sw_check_ticks   = pdMS_TO_TICKS(CORE0_SW_STROBE_MS);
    const TickType_t sw_timeout_ticks = pdMS_TO_TICKS(CORE0_SW_TIMEOUT_MS);

    // Timing variables for SW watchdog
    TickType_t start;
    TickType_t end;

    // Main Slow Core loop
    while (1)
    {
        // Start SW watchdog timing
        start = xTaskGetTickCount();

        app_states_exec();

        // End SW watchdog timing and check for timeout
        end = xTaskGetTickCount();
        // If Slow Core task overran SW watchdog timeout, disable TWDT feeding
        if ((end - start) > sw_timeout_ticks) {
            BMS_LOGE("Slow Core SW watchdog timeout (> %d ms), disabling HW WD feed", CORE0_SW_TIMEOUT_MS);
            s_allow_feeding = false;
        }

        // Puts task into blocked state for defined period
        vTaskDelay(sw_check_ticks);
    }
}


/// Slow Core TWDT feeder task. This task periodically feeds (resets) the hardware TWDT to prevent timeout.
/// Feeding is only performed if \ref s_allow_feeding flag is true, otherwise feeding is skipped, allowing TWDT to expire
/// and reset the system.
///
/// \param arg Unused in current implementation and may be NULL
/// \return None
static void slow_core_feeder_task()
{
    // Register current task with TWDT. If registration fails, delete task.
    if (bms_wdt_register_current_task() != ESP_OK) {
        BMS_LOGE("Failed to register Slow Core feeder to TWDT");
        vTaskDelete(NULL);
    }

    const TickType_t feed_ticks = pdMS_TO_TICKS(WDT_FEED_MS);

    // Main Slow Core feeder loop. Periodically feed TWDT if allowed.
    while (1)
    {
        // Variable_allow_feeding indicates whether feeding is allowed. Variable can be set to false
        // by Slow Core tasks on error conditions.
        if (s_allow_feeding) {
            if (bms_wdt_feed_self() != ESP_OK) {
                BMS_LOGE("HW WD feed failed (Slow Core feeder)");
            }
        }
        // Put task into blocked state for defined period
        vTaskDelay(feed_ticks);
    }
}
