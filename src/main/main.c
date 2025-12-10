/// This is the main application module. It initializes all necessary modules and starts application tasks.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "logging.h"
#include "watchdog.h"
#include "app_tasks.h"
#include "bms_adapter.h"
#include "intercore_comm.h"
#include "logging.h"
#include "wifi.h"
#include "mqtt.h"

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
    // Initialize logging system
    bms_logging_init();

     esp_err_t err;

    // Initialize WiFi in station mode to connect to MQTT broker on remote server.
    err = bms_wifi_init();
    if (err != ESP_OK) {
        BMS_LOGE("WiFi init failed: %s", esp_err_to_name(err));
        return;
    }

    // MQTT initialization must be done after WiFi is initialized. Connecting to broker is done in MQTT module.
    err = bms_mqtt_init();
    if (err != ESP_OK) {
        BMS_LOGE("MQTT init failed: %s", esp_err_to_name(err));
        return;
    }

    // Initialize software watchdog (TWDT)
    err = bms_wdt_init();
    if (err != ESP_OK) {
        BMS_LOGE("Watchdog init failed: %s", esp_err_to_name(err));
        return;
    }

    // Select and initialize BMS adapter (demo adapter in current implementation)
    err = bms_demo_adapter_select();
    if (err != ESP_OK) {
        BMS_LOGE("BMS adapter init failed: %s", esp_err_to_name(err));
        return;
    }

    // Initialize inter-core communication queue
    bms_queue_init();

    // Create application tasks
    err = app_tasks_create();
    if (err != ESP_OK) {
        BMS_LOGE("App tasks creation failed");
        return;
    }

    BMS_LOGI("Application started, tasks running");
}

