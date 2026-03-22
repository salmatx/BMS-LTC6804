/// This is the initialization module. It initializes all necessary modules and starts application tasks.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "logging.h"
#include "tasksSC.h"
#include "tasksFC.h"
#include "bms_adapter.h"
#include "configuration.h"
#include "intercore_comm.h"
#include "logging.h"
#include "wifi.h"
#include "mqtt.h"
#include "http_server.h"
#include "telemetry.h"
#include "stats_history.h"


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

    // Initialize telemetry module (caches device ID and SW version)
    telemetry_init();

    // Initialize latest-sample store for HTTP stats endpoint
    err = bms_stats_hist_init();
    if (err != ESP_OK) {
        BMS_LOGE("Stats history init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Initialize WiFi in station mode to connect to MQTT broker on remote server.
    // If connection fails, falls back to AP mode for configuration.
    err = bms_wifi_init();
    if (err != ESP_OK) {
        BMS_LOGE("WiFi init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Start HTTP server regardless of WiFi mode (needed for configuration in AP mode)
    err = http_server_start();
    if (err != ESP_OK) return false;

    // If WiFi is in AP mode (fallback), enter CONFIG state for user configuration
    if (bms_wifi_is_ap_mode()) {
        BMS_LOGI("WiFi in AP mode - entering CONFIG state for user configuration");
        
        // Set NVS flag to enter CONFIG state
        nvs_handle_t nvs_handle;
        err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            nvs_set_u8(nvs_handle, "config_mode", 1);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
            BMS_LOGI("CONFIG mode flag set in NVS");
        } else {
            BMS_LOGW("Failed to set CONFIG mode flag: %s", esp_err_to_name(err));
        }
        
        // Return false to trigger CONFIG state, skip BMS initialization
        return false;
    }

    // Normal STA mode - continue with full initialization
    // MQTT initialization must be done after WiFi is initialized. Connecting to broker is done in MQTT module.
    err = bms_mqtt_init();
    if (err != ESP_OK) {
        BMS_LOGE("MQTT init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Select and initialize BMS adapter based on configuration
    if (g_cfg.battery.adapter_mode == BMS_ADAPTER_DEMO) {
        err = bms_demo_adapter_select();
    } else {
        err = bms_ltc6804_adapter_select();
    }
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

