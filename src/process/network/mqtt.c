/// This module implements MQTT communication.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"
#include "mqtt.h"
#include "configuration.h"

#include "freertos/FreeRTOS.h"

#include "mqtt_client.h"
#include "esp_err.h"
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
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Handle to MQTT client.
static esp_mqtt_client_handle_t s_mqtt = NULL;

/// Flag indicating whether MQTT client is connected to the broker.
static volatile bool s_connected = false;

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
        .broker.address.uri       = g_cfg.mqtt.uri,
        .credentials.client_id    = "esp32-bms",
        .network.timeout_ms = 30000,
        .session.keepalive = 60,
    };

    // Initialize MQTT client
    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        BMS_LOGE("Failed to init MQTT client");
        return ESP_FAIL;
    }

    // Register MQTT event handler
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    // Start MQTT client (connect to broker and start communication)
    esp_err_t err = esp_mqtt_client_start(s_mqtt);
    if (err != ESP_OK) {
        BMS_LOGE("Failed to start MQTT: %s", esp_err_to_name(err));
        return err;
    }

    BMS_LOGI("MQTT client started");
    return ESP_OK;
}

/// This function checks whether the MQTT client is currently connected to the broker.
///
/// \param None
/// \return true if connected, false otherwise
bool bms_mqtt_is_connected(void)
{
    return s_connected;
}

/// This function publishes a message with QoS 0 (fire-and-forget, no ACK).
///
/// Notes:
/// 1. This function requires the MQTT client to be connected.
/// 2. Returns immediately after sending, does not wait for broker response.
///
/// \param topic Pointer to topic string
/// \param data Pointer to payload buffer
/// \param len Payload length
/// \return ESP_OK on successful send, otherwise error
esp_err_t bms_mqtt_publish_qos0(const char *topic, const char *data, int len)
{
    if (!s_mqtt || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    // Publish with QoS 0, no PUBACK is expected.
    int msg_id = esp_mqtt_client_publish(s_mqtt, topic, data, len, 0, 0);
    
    if (msg_id < 0) {
        BMS_LOGE("MQTT publish failed (msg_id=%d)", msg_id);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// This function is MQTT event handler. Tracks connection state. Logs connection and disconnection events.
/// Note that parameters 'handler_args' and 'base' are unused and cannot be removed because this function is used
/// as a callback with fixed signature of ESP-IDF event handler.
///
/// \param handler_args Unused
/// \param base Unused
/// \param event_id MQTT event ID
/// \param event_data Pointer to event data
/// \return None
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (!event) {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        BMS_LOGI("MQTT connected");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        BMS_LOGW("MQTT disconnected");
        break;

    case MQTT_EVENT_ERROR:
        BMS_LOGE("MQTT event error");
        break;

    default:
        break;
    }
}
