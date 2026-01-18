/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"

#include "wifi.h"
#include "network_configuration.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "configuration.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_WIFI"
/// Bit group to signal when WiFi is connected
#define WIFI_CONNECTED_BIT BIT0

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Event group to signal when WiFi is connected
static EventGroupHandle_t s_wifi_event_group;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/
/// This function initializes the WiFi in station mode and connects to the configured AP.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_wifi_init(void)
{
    // Create the event group to handle WiFi events
    s_wifi_event_group = xEventGroupCreate();

    // Initialize the TCP/IP stack. Must be called before esp_netif_create_default_wifi_sta()
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that handles WiFi events
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // Create default WiFi station and attach it to TCP/IP stack
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers for WiFi and IP events
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    // Configure WiFi connection settings.
    wifi_config_t wifi_cfg = { 0 };
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", g_cfg.wifi.ssid);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", g_cfg.wifi.pass);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // Set WiFi mode to station and apply configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        BMS_LOGE("WiFi connect timeout");
        return ESP_FAIL;
    }

    BMS_LOGI("WiFi connected");
    return ESP_OK;
}

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// WiFi event handler to manage connection events.
///
/// \param arg Unused in current implementation and may be NULL
/// \param event_base Base of the event
/// \param event_id ID of the event
/// \param event_data Data associated with the event
/// \return None
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // TODO: remove parameters if unused in future implementation
    (void)arg;
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        BMS_LOGI("Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
