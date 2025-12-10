/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"

#include "mqtt.h"
#include "network_configuration.h"

#include "esp_log.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_MQTT"

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
/// Handle to MQTT client.
static esp_mqtt_client_handle_t s_mqtt = NULL;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes the MQTT client and connects to the broker.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_mqtt_init(void)
{
    // MQTT client configuration
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = BMS_MQTT_BROKER_URI,
        .credentials.client_id = "esp32-bms",
    };

    // Initialize MQTT client
    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        BMS_LOGE("Failed to init MQTT client");
        return ESP_FAIL;
    }

    // Start MQTT client (connect to broker and start communication)
    esp_err_t err = esp_mqtt_client_start(s_mqtt);
    if (err != ESP_OK) {
        BMS_LOGE("Failed to start MQTT: %s", esp_err_to_name(err));
        return err;
    }

    BMS_LOGI("MQTT client started");
    return ESP_OK;
}

/// This function returns the handle to the initialized MQTT client.
///
/// \param None
/// \return MQTT client handle
esp_mqtt_client_handle_t bms_mqtt_get_client(void)
{
    return s_mqtt;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/