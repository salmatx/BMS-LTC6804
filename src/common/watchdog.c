/// This file implements the task watchdog (TWDT) functionality for the BMS application.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "watchdog.h"
#include "logging.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "sdkconfig.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_WDT"

/// Global TWDT timeout in milliseconds.
#ifndef CONFIG_BMS_WDT_TIMEOUT_MS
#define CONFIG_BMS_WDT_TIMEOUT_MS 80
#endif

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes the internal Task Watchdog (TWDT).
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_wdt_init(void)
{
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = CONFIG_BMS_WDT_TIMEOUT_MS,    // timeout in ms
        .idle_core_mask = 0,                        // no idle tasks watched
        .trigger_panic = true                       // panic + reset on timeout
    };

    // Initialize TWDT
    esp_err_t err = esp_task_wdt_init(&twdt_config);
    if (err != ESP_OK) {
        BMS_LOGE("esp_task_wdt_init failed: %s", esp_err_to_name(err));
        return err;
    }
    BMS_LOGI("Task WDT initialized: timeout=%d ms", CONFIG_BMS_WDT_TIMEOUT_MS);
    return ESP_OK;

}

/// This function registers the current task with TWDT. To initialize TWDT call this function from the task.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_wdt_register_current_task(void)
{
    TaskHandle_t th = xTaskGetCurrentTaskHandle();
    if (th == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_task_wdt_add(th);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        BMS_LOGE("esp_task_wdt_add failed for task \"%s\": %s",
                     pcTaskGetName(th), esp_err_to_name(err));
        return err;
    }

    BMS_LOGI("Task \"%s\" registered to TWDT", pcTaskGetName(th));
    return ESP_OK;
}

/// This function feeds (resets) the TWDT for the current task. Call periodically from the task to prevent timeout.
/// Call interval must be less than the configured timeout otherwise a timeout will occur and MCU will reset.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_wdt_feed_self(void)
{
    esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        BMS_LOGE("esp_task_wdt_reset failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

/// This function unregisters the current task from TWDT. Call before deleting the task otherwise TWDT will trigger a timeout.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_wdt_unregister_current_task(void)
{
    TaskHandle_t th = xTaskGetCurrentTaskHandle();
    if (th == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_task_wdt_delete(th);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        BMS_LOGE("esp_task_wdt_delete failed for task \"%s\": %s",
                     pcTaskGetName(th), esp_err_to_name(err));
        return err;
    }

    BMS_LOGI("Task \"%s\" unregistered from TWDT", pcTaskGetName(th));
    return ESP_OK;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/