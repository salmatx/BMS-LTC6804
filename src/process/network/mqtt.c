/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"
#include "mqtt.h"
#include "network_configuration.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "mqtt_client.h"
#include "esp_err.h"
#include "esp_log.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_MQTT"

/// Event bit 0 is set when broker ACKs a QoS1/2 publish (MQTT_EVENT_PUBLISHED)
#define MQTT_EV_PUBLISHED_BIT 1U

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

/// Event group used to wait for publish ACK (QoS1/2).
static EventGroupHandle_t s_mqtt_ev = NULL;

/// Last publish message id we are waiting to be ACKed by broker.
static volatile int s_last_pub_msg_id = -1;

/// True if MQTT client is connected.
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
    // Create event group for publish ACK signaling
    s_mqtt_ev = xEventGroupCreate();
    if (!s_mqtt_ev) {
        BMS_LOGE("Failed to create MQTT event group");
        return ESP_ERR_NO_MEM;
    }

    // MQTT client configuration
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri       = BMS_MQTT_BROKER_URI,
        .credentials.client_id    = "esp32-bms",
    };

    // Initialize MQTT client
    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        BMS_LOGE("Failed to init MQTT client");
        return ESP_FAIL;
    }

    // Register MQTT event handler (needed for MQTT_EVENT_PUBLISHED handling)
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

/// This function returns true if MQTT client is connected to the broker.
///
/// \param None
/// \return true if connected, false otherwise
bool bms_mqtt_is_connected(void)
{
    return s_connected;
}

/// This function publishes a message with QoS1 and block until broker ACK is received (MQTT_EVENT_PUBLISHED) or timeout.
///
/// Notes:
/// - This function requires the MQTT client to be connected.
/// - A return value of ESP_OK means the broker acknowledged the publish (QoS1 PUBACK).
///
/// \param topic MQTT topic
/// \param data Pointer to payload buffer
/// \param len Payload length
/// \param timeout_ticks How long to wait for ACK
/// \return ESP_OK on ACK, ESP_ERR_TIMEOUT on timeout, otherwise error
esp_err_t bms_mqtt_publish_blocking_qos1(const char *topic, const char *data, int len, TickType_t timeout_ticks)
{
    if (!s_mqtt || !s_mqtt_ev || !s_connected) {
        return ESP_FAIL;
    }

    // Clear ACK bit from any previous operation
    xEventGroupClearBits(s_mqtt_ev, MQTT_EV_PUBLISHED_BIT);

    // Publish with QoS1 so MQTT_EVENT_PUBLISHED is emitted on PUBACK
    int msg_id = esp_mqtt_client_publish(s_mqtt, topic, data, len, 1, 0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    s_last_pub_msg_id = msg_id;

    // Wait until event handler signals publish ACK
    EventBits_t bits = xEventGroupWaitBits(
        s_mqtt_ev,
        MQTT_EV_PUBLISHED_BIT,
        pdTRUE,   // clear on exit
        pdTRUE,   // wait all
        timeout_ticks
    );

    return (bits & MQTT_EV_PUBLISHED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// This function is MQTT event handler. Tracks connection state and signals when a QoS1/2 publish was ACKed by broker.
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

    case MQTT_EVENT_PUBLISHED:
        // For QoS1/2, ESP-IDF posts MQTT_EVENT_PUBLISHED when publish is acknowledged.
        if (s_mqtt_ev && (event->msg_id == s_last_pub_msg_id)) {
            xEventGroupSetBits(s_mqtt_ev, MQTT_EV_PUBLISHED_BIT);
        }
        break;

    case MQTT_EVENT_ERROR:
        BMS_LOGE("MQTT event error");
        break;

    default:
        break;
    }
}
