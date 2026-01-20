/// This module implements WiFi connectivity.

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
#include "lwip/sockets.h"
#include "configuration.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_WIFI"
/// Bit group to signal when WiFi is connected
#define WIFI_CONNECTED_BIT BIT0
/// Default netmask if none is configured and static IP is used
#define DEFAULT_NETMASK "255.255.255.0"

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
/// This function initializes the WiFi in station mode and connects to the configured AP. If static IP is configured,
/// it attempts to set it before connecting. Function performs following steps:
/// 1. Create event group for WiFi events.
/// 2. Initialize TCP/IP stack.
/// 3. Create default event loop for WiFi events.
/// 4. Create default WiFi station network interface and start DHCP client.
/// 5. If static IP is configured, stop DHCP client and set static IP, gateway, and netmask.
///    If setting static IP fails, continue using DHCP.
/// 6. Initialize WiFi with default configuration.
/// 7. Register event handlers for WiFi and IP events.
/// 8. Configure WiFi connection settings (SSID, password, auth mode).
/// 9. Set WiFi mode to station and apply configuration.
/// 10. Start WiFi.
/// 11. Wait for connection or timeout.
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
    // Create default WiFi station and attach it to TCP/IP stack. Starts DHCP client automatically.
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    // Check if static IP is configured and valid
    bool use_static_ip = false;
    if (g_cfg.wifi.static_ip[0] != '\0' && strlen(g_cfg.wifi.static_ip) > 0) {
        BMS_LOGI("Attempting to configure static IP: %s", g_cfg.wifi.static_ip);
        
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(ip_info));
        
        // Parse static IP (required)
        if (inet_pton(AF_INET, g_cfg.wifi.static_ip, &ip_info.ip) != 1) {
            BMS_LOGW("Invalid static IP address format, using DHCP");
        } else {
            // Parse netmask (use default 255.255.255.0 if empty)
            if (g_cfg.wifi.netmask[0] == '\0' || strlen(g_cfg.wifi.netmask) == 0) {
                BMS_LOGI("Netmask not configured, using default %s", DEFAULT_NETMASK);
                inet_pton(AF_INET, DEFAULT_NETMASK, &ip_info.netmask);
            } else if (inet_pton(AF_INET, g_cfg.wifi.netmask, &ip_info.netmask) != 1) {
                BMS_LOGW("Invalid netmask format, using default %s", DEFAULT_NETMASK);
                inet_pton(AF_INET, DEFAULT_NETMASK, &ip_info.netmask);
            }
            
            // Parse gateway (optional - set to 0.0.0.0 if empty)
            if (g_cfg.wifi.gateway[0] == '\0' || strlen(g_cfg.wifi.gateway) == 0) {
                BMS_LOGI("Gateway not configured, local network only");
                ip_info.gw.addr = 0;
            } else if (inet_pton(AF_INET, g_cfg.wifi.gateway, &ip_info.gw) != 1) {
                BMS_LOGW("Invalid gateway format, setting to none");
                ip_info.gw.addr = 0;
            }
            
            // Stop DHCP client (started automatically by esp_netif_create_default_wifi_sta)
            esp_err_t err = esp_netif_dhcpc_stop(netif);
            if (err == ESP_OK || err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
                // Try to set static IP
                err = esp_netif_set_ip_info(netif, &ip_info);
                if (err == ESP_OK) {
                    BMS_LOGI("Static IP configured successfully");
                    use_static_ip = true;
                } else {
                    BMS_LOGW("Failed to set static IP: %s, restarting DHCP", esp_err_to_name(err));
                    esp_netif_dhcpc_start(netif);
                }
            } else {
                BMS_LOGW("Failed to stop DHCP client: %s, falling back to DHCP", esp_err_to_name(err));
            }
        }
    } else {
        BMS_LOGI("No static IP configured, using DHCP");
    }

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
/// Note that paramter 'arg' is unused and cannot be removed because this function is used as a callback with
/// fixed signature of ESP-IDF event handler.
///
/// \param arg Unused
/// \param event_base Base of the event
/// \param event_id ID of the event
/// \param event_data Data associated with the event
/// \return None
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
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
