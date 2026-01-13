/// This is the initialization module. It initializes all necessary modules and starts application tasks.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "logging.h"
#include "tasksSC.h"
#include "tasksFC.h"
#include "bms_adapter.h"
#include "intercore_comm.h"
#include "logging.h"
#include "wifi.h"
#include "mqtt.h"
#include "http_server.h"


/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "INITIALIZATION"

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

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// Application initialization. Initializes all necessary modules and starts application tasks.
///
/// \param None
/// \return true on success, false on failure
bool initialization_exec(void)
{
     esp_err_t err;

    // Initialize WiFi in station mode to connect to MQTT broker on remote server.
    err = bms_wifi_init();
    if (err != ESP_OK) {
        BMS_LOGE("WiFi init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = bms_http_server_start();
    if (err != ESP_OK) return false;

    // MQTT initialization must be done after WiFi is initialized. Connecting to broker is done in MQTT module.
    err = bms_mqtt_init();
    if (err != ESP_OK) {
        BMS_LOGE("MQTT init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Select and initialize BMS adapter (demo adapter in current implementation)
    err = bms_demo_adapter_select();
    if (err != ESP_OK) {
        BMS_LOGE("BMS adapter init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Initialize inter-core communication queue
    bms_queue_init();

    // Create Fast Core tasks
    err = fast_core_tasks_create();
    if (err != ESP_OK) {
        BMS_LOGE("Fast Core tasks creation failed");
        return false;
    }

    BMS_LOGI("Application started, tasks running");

    return true;
}

