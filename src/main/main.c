/// This is the main application module. It initializes all necessary modules and starts application tasks.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "logging.h"
#include "tasksSC.h"
#include "watchdog.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "MAIN"

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
void app_main(void);

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

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// Main application entry point. Initializes all necessary modules and starts application tasks.
///
/// \param None
/// \return None
void app_main(void)
{
    // Initialize NVS first (required for config mode flag and WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize logging system
    bms_logging_init();

    esp_err_t err;

    // Initialize software watchdog (TWDT)
    err = bms_wdt_init();
    if (err != ESP_OK) {
        BMS_LOGE("Watchdog init failed: %s", esp_err_to_name(err));
        return;
    }

    // Create Slow Core tasks
    err = slow_core_task_create();
    if (err != ESP_OK) {
        BMS_LOGE("Slow Core tasks creation failed");
        return;
    }

    BMS_LOGI("Application started.");
}

