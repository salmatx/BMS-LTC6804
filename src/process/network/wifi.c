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
/// AP mode SSID when WiFi connection fails
#define AP_SSID "BMS_LTC6804"
/// AP mode password (minimum 8 characters for WPA2)
#define AP_PASSWORD "bms12345"
/// Maximum number of stations allowed to connect to AP
#define AP_MAX_CONNECTIONS 4

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
/// Flag to track if device is running in AP mode
static bool s_is_ap_mode = false;

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
        BMS_LOGW("WiFi STA mode connect timeout, switching to AP mode");
        
        // Clean up STA mode
        esp_wifi_stop();
        esp_wifi_deinit();
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
        
        // Destroy STA netif
        esp_netif_destroy(netif);
        
        // Create AP network interface (required for DHCP server and IP routing)
        esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
        if (!ap_netif) {
            BMS_LOGE("Failed to create AP network interface");
            return ESP_FAIL;
        }
        
        // Reinitialize WiFi for AP mode
        wifi_init_config_t cfg_ap = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg_ap));
        
        // Start AP mode as fallback
        esp_err_t err = bms_wifi_start_ap();
        if (err != ESP_OK) {
            BMS_LOGE("Failed to start AP mode: %s", esp_err_to_name(err));
            return ESP_FAIL;
        }
        
        s_is_ap_mode = true;
        return ESP_OK;
    }

    s_is_ap_mode = false;
    BMS_LOGI("WiFi connected in STA mode");
    return ESP_OK;
}

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function starts WiFi in Access Point (AP) mode with predefined credentials.
/// Used as fallback when STA mode connection fails.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_wifi_start_ap(void)
{
    // Configure AP mode
    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .max_connection = AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = 1,
        },
    };

    // Set WiFi mode to AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    BMS_LOGI("╔══════════════════════════════════════════════════════╗");
    BMS_LOGI("║         WiFi AP MODE - Configuration Portal          ║");
    BMS_LOGI("╠══════════════════════════════════════════════════════╣");
    BMS_LOGI("║  SSID:     %s                               ║", AP_SSID);
    BMS_LOGI("║  Password: %s                                  ║", AP_PASSWORD);
    BMS_LOGI("║  IP:       192.168.4.1                               ║");
    BMS_LOGI("║  HTTP:     http://192.168.4.1                        ║");
    BMS_LOGI("╠══════════════════════════════════════════════════════╣");
    BMS_LOGI("║  Steps:                                              ║");
    BMS_LOGI("║  1. Connect to WiFi '%s'                    ║", AP_SSID);
    BMS_LOGI("║  2. Open browser: http://192.168.4.1                 ║");
    BMS_LOGI("║  3. Configure WiFi credentials                       ║");
    BMS_LOGI("║  4. Device will restart with new settings            ║");
    BMS_LOGI("╚══════════════════════════════════════════════════════╝");

    return ESP_OK;
}

/// This function checks if WiFi is currently running in AP mode.
///
/// \param None
/// \return true if in AP mode, false if in STA mode
bool bms_wifi_is_ap_mode(void)
{
    return s_is_ap_mode;
}

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
