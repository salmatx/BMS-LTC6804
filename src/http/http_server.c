/// This module implements HTTP server functionality for displaying and configuring BMS parameters and data
/// retrieved from BMS.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "http_server.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "logging.h"
#include "stats_history.h"
#include "configuration.h"
#include "cJSON.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_HTTP"

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static esp_err_t send_file(httpd_req_t *req, const char *path, const char *ctype);
static esp_err_t parse_post_param(const char *buf, const char *key, char *value, size_t value_len);
static bool is_valid_ip(const char *ip_str);
static esp_err_t send_error_modal(httpd_req_t *req, const char *title, const char *message);

// Handlers for HTTP endpoints
static esp_err_t h_config_data(httpd_req_t *req);
static esp_err_t h_config_save(httpd_req_t *req);
static esp_err_t h_config_cancel(httpd_req_t *req);
static esp_err_t h_root_redirect(httpd_req_t *req);
static esp_err_t h_index(httpd_req_t *req);
static esp_err_t h_stats_page(httpd_req_t *req);
static esp_err_t h_config_page(httpd_req_t *req);
static esp_err_t h_js_charts(httpd_req_t *req);
static esp_err_t h_css_style(httpd_req_t *req);
static esp_err_t h_stats_data(httpd_req_t *req);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Handler for serving static files. Serves as the main HTTP server instance.
static httpd_handle_t s_httpd = NULL;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function starts the HTTP server and registers all HTTP endpoints.
///
/// param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t http_server_start(void)
{
    if (s_httpd) return ESP_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.core_id = 0;
    cfg.stack_size = 8192;
    cfg.task_priority = 4;
    cfg.max_uri_handlers = 12;

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        s_httpd = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t u_root          = { .uri = "/",                     .method = HTTP_GET,  .handler = h_root_redirect };
    httpd_uri_t u_bms           = { .uri = "/bms",                  .method = HTTP_GET,  .handler = h_index };
    httpd_uri_t u_stats         = { .uri = "/bms/stats",            .method = HTTP_GET,  .handler = h_stats_page };
    httpd_uri_t u_cfg           = { .uri = "/bms/config",           .method = HTTP_GET,  .handler = h_config_page };
    httpd_uri_t u_js            = { .uri = "/bms/js/charts.js",     .method = HTTP_GET,  .handler = h_js_charts };
    httpd_uri_t u_data          = { .uri = "/bms/stats/data",       .method = HTTP_GET,  .handler = h_stats_data };
    httpd_uri_t u_cfg_data      = { .uri = "/bms/config/data",      .method = HTTP_GET,  .handler = h_config_data };
    httpd_uri_t u_cfg_save      = { .uri = "/bms/config/save",      .method = HTTP_POST, .handler = h_config_save };
    httpd_uri_t u_cfg_cancel    = { .uri = "/bms/config/cancel",    .method = HTTP_POST, .handler = h_config_cancel };
    httpd_uri_t u_css           = { .uri = "/bms/css/style.css",    .method = HTTP_GET,  .handler = h_css_style };
    
    httpd_register_uri_handler(s_httpd, &u_root);
    httpd_register_uri_handler(s_httpd, &u_bms);
    httpd_register_uri_handler(s_httpd, &u_stats);
    httpd_register_uri_handler(s_httpd, &u_cfg);
    httpd_register_uri_handler(s_httpd, &u_js);
    httpd_register_uri_handler(s_httpd, &u_data);
    httpd_register_uri_handler(s_httpd, &u_cfg_data);
    httpd_register_uri_handler(s_httpd, &u_cfg_save);
    httpd_register_uri_handler(s_httpd, &u_cfg_cancel);
    httpd_register_uri_handler(s_httpd, &u_css);

    BMS_LOGI("HTTP server started");
    return ESP_OK;
}

/// This function stops the HTTP server.
///
/// param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t http_server_stop(void)
{
    if (!s_httpd) return ESP_OK;
    httpd_stop(s_httpd);
    s_httpd = NULL;
    return ESP_OK;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// This function handles sending a static file to the HTTP client. Function is used within created HTTP endpoint.
///
/// \param req Pointer to HTTP request structure
/// \param path Path to the file to send
/// \param ctype Content type of the file
/// \return ESP_OK on success, otherwise an error code
static esp_err_t send_file(httpd_req_t *req, const char *path, const char *ctype)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }

    httpd_resp_set_type(req, ctype);

    char chunk[1024];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }

    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/// This function parses a single URL-encoded POST data and converts it to a string value.
/// It is helper function used in configuration saving endpoint.
///
/// \param buf Pointer to buffer containing URL-encoded POST data
/// \param key Key of the parameter to parse
/// \param value Pointer to buffer where the parsed value will be stored
/// \param value_len Length of the value buffer
/// \return ESP_OK on success, otherwise an error code
static esp_err_t parse_post_param(const char *buf, const char *key, char *value, size_t value_len)
{
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    
    const char *ptr = strstr(buf, search_key);
    if (ptr == NULL) return ESP_ERR_NOT_FOUND;
    
    ptr += strlen(search_key);
    const char *end = strchr(ptr, '&');
    size_t len = end ? (size_t)(end - ptr) : strlen(ptr);
    
    if (len >= value_len) len = value_len - 1;
    memcpy(value, ptr, len);
    value[len] = '\0';
    
    // URL decode
    char *src = value;
    char *dst = value;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            int val;
            sscanf(src + 1, "%2x", &val);
            *dst++ = (char)val;
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    return ESP_OK;
}

/// Sends an error modal window to the user with a custom title and message.
/// Loads HTML template from file and replaces placeholders with actual values.
///
/// \param req Pointer to HTTP request structure
/// \param title Title of the error modal
/// \param message Error message to display
/// \return ESP_FAIL always (to indicate validation error)
static esp_err_t send_error_modal(httpd_req_t *req, const char *title, const char *message)
{
    // Load HTML template from file
    FILE *f = fopen("/spiffs/bms/error_modal.html", "rb");
    if (!f) {
        // Fallback to simple error message if file not found
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error loading error template");
        return ESP_FAIL;
    }

    // Read template into buffer
    char template[2048];
    size_t len = fread(template, 1, sizeof(template) - 1, f);
    fclose(f);
    template[len] = '\0';

    // Replace placeholders {{TITLE}} and {{MESSAGE}}
    char response[2048];
    char *src = template;
    char *dst = response;
    char *end = response + sizeof(response) - 1;

    while (*src && dst < end) {
        if (strncmp(src, "{{TITLE}}", 9) == 0) {
            size_t title_len = strlen(title);
            if (dst + title_len < end) {
                strcpy(dst, title);
                dst += title_len;
            }
            src += 9;
        } else if (strncmp(src, "{{MESSAGE}}", 11) == 0) {
            size_t msg_len = strlen(message);
            if (dst + msg_len < end) {
                strcpy(dst, message);
                dst += msg_len;
            }
            src += 11;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    httpd_resp_sendstr(req, response);
    return ESP_FAIL;
}

/// This is the GET handler for retrieving current configuration data as JSON response. It builds JSON object
/// containing current configuration parameters and sends it to the HTTP client.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_config_data(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *mqtt = cJSON_CreateObject();
    cJSON *bat  = cJSON_CreateObject();

    cJSON_AddItemToObject(root, "wifi", wifi);
    cJSON_AddStringToObject(wifi, "ssid", g_cfg.wifi.ssid);
    cJSON_AddStringToObject(wifi, "static_ip", g_cfg.wifi.static_ip);
    cJSON_AddStringToObject(wifi, "gateway", g_cfg.wifi.gateway);
    cJSON_AddStringToObject(wifi, "netmask", g_cfg.wifi.netmask);

    cJSON_AddItemToObject(root, "mqtt", mqtt);
    cJSON_AddStringToObject(mqtt, "uri", g_cfg.mqtt.uri);

    cJSON_AddItemToObject(root, "battery", bat);
    cJSON_AddNumberToObject(bat, "cell_v_min", g_cfg.battery.cell_v_min);
    cJSON_AddNumberToObject(bat, "cell_v_max", g_cfg.battery.cell_v_max);
    cJSON_AddNumberToObject(bat, "pack_v_min", g_cfg.battery.pack_v_min);
    cJSON_AddNumberToObject(bat, "pack_v_max", g_cfg.battery.pack_v_max);
    cJSON_AddNumberToObject(bat, "current_min", g_cfg.battery.current_min);
    cJSON_AddNumberToObject(bat, "current_max", g_cfg.battery.current_max);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!out) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
    }

    esp_err_t err = httpd_resp_sendstr(req, out);
    free(out);
    return err;
}

/// This is the POST handler for saving configuration data sent by the HTTP client. Function performs following steps:
/// 1. Receives POST data containing URL-encoded configuration parameters.
/// 2. Parses individual parameters and updates global configuration structure.
/// 3. Saves updated configuration to file.
/// 4. Clears configuration mode flag in NVS to prevent re-entering config mode on next boot.
/// 5. Sends success response with auto-redirect to main BMS page.
/// 6. Restarts the ESP32 to apply new configuration.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_config_save(httpd_req_t *req)
{
    char buf[2048];
    char value[128];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    BMS_LOGI("Received config save request: %s", buf);
    
    // Parse WiFi parameters
    if (parse_post_param(buf, "wifi_ssid", value, sizeof(value)) == ESP_OK) {
        BMS_LOGI("Parsed wifi_ssid: %s", value);
        strncpy(g_cfg.wifi.ssid, value, sizeof(g_cfg.wifi.ssid) - 1);
        g_cfg.wifi.ssid[sizeof(g_cfg.wifi.ssid) - 1] = '\0';
    }

    // Parse static IP (optional - empty means use DHCP)
    if (parse_post_param(buf, "wifi_static_ip", value, sizeof(value)) == ESP_OK) {
        // Validate IP format if not empty
        if (strlen(value) > 0 && !is_valid_ip(value)) {
            BMS_LOGW("Invalid static IP format: %s", value);
            return send_error_modal(req, "Invalid Static IP Address", 
                "The IP address format is invalid. Please enter a valid IPv4 address (e.g., 192.168.1.100).");
        }
        strncpy(g_cfg.wifi.static_ip, value, sizeof(g_cfg.wifi.static_ip) - 1);
        g_cfg.wifi.static_ip[sizeof(g_cfg.wifi.static_ip) - 1] = '\0';
    }
    
    if (parse_post_param(buf, "wifi_gateway", value, sizeof(value)) == ESP_OK) {
        // Validate IP format if not empty
        if (strlen(value) > 0 && !is_valid_ip(value)) {
            BMS_LOGW("Invalid gateway format: %s", value);
            return send_error_modal(req, "Invalid Gateway Address", 
                "The gateway address format is invalid. Please enter a valid IPv4 address (e.g., 192.168.1.1).");
        }
        strncpy(g_cfg.wifi.gateway, value, sizeof(g_cfg.wifi.gateway) - 1);
        g_cfg.wifi.gateway[sizeof(g_cfg.wifi.gateway) - 1] = '\0';
    }
    
    if (parse_post_param(buf, "wifi_netmask", value, sizeof(value)) == ESP_OK) {
        // Validate IP format if not empty
        if (strlen(value) > 0 && !is_valid_ip(value)) {
            BMS_LOGW("Invalid netmask format: %s", value);
            return send_error_modal(req, "Invalid Netmask", 
                "The netmask format is invalid. Please enter a valid IPv4 netmask (e.g., 255.255.255.0).");
        }
        strncpy(g_cfg.wifi.netmask, value, sizeof(g_cfg.wifi.netmask) - 1);
        g_cfg.wifi.netmask[sizeof(g_cfg.wifi.netmask) - 1] = '\0';
    }

    // Only update password if provided (not empty)
    if (parse_post_param(buf, "wifi_pass", value, sizeof(value)) == ESP_OK) {
        if (strlen(value) > 0) {
            BMS_LOGI("Updating wifi password");
            strncpy(g_cfg.wifi.pass, value, sizeof(g_cfg.wifi.pass) - 1);
            g_cfg.wifi.pass[sizeof(g_cfg.wifi.pass) - 1] = '\0';
        } else {
            BMS_LOGI("Password field empty, keeping existing password");
        }
    }
    
    // Parse MQTT parameters
    if (parse_post_param(buf, "mqtt_uri", value, sizeof(value)) == ESP_OK) {
        strncpy(g_cfg.mqtt.uri, value, sizeof(g_cfg.mqtt.uri) - 1);
        g_cfg.mqtt.uri[sizeof(g_cfg.mqtt.uri) - 1] = '\0';
    }
    
    // Parse battery parameters and round to 2 decimal places
    if (parse_post_param(buf, "cell_v_min", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.cell_v_min = roundf(atof(value) * 100.0f) / 100.0f;
    }
    if (parse_post_param(buf, "cell_v_max", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.cell_v_max = roundf(atof(value) * 100.0f) / 100.0f;
    }
    if (parse_post_param(buf, "pack_v_min", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.pack_v_min = roundf(atof(value) * 100.0f) / 100.0f;
    }
    if (parse_post_param(buf, "pack_v_max", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.pack_v_max = roundf(atof(value) * 100.0f) / 100.0f;
    }
    if (parse_post_param(buf, "current_min", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.current_min = roundf(atof(value) * 100.0f) / 100.0f;
    }
    if (parse_post_param(buf, "current_max", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.current_max = roundf(atof(value) * 100.0f) / 100.0f;
    }
    
    // Save configuration to file
    esp_err_t err = configuration_save("/spiffs/config.json");
    if (err != ESP_OK) {
        BMS_LOGE("Failed to save configuration: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
        return ESP_FAIL;
    }
    
    // Clear config mode flag
    nvs_handle_t nvs_handle;
    if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, "config_mode", 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        BMS_LOGI("Config mode flag cleared");
    }
    
    // Send success response with auto-redirect
    httpd_resp_set_type(req, "text/html");
    esp_err_t send_err = send_file(req, "/spiffs/bms/config_saved.html", "text/html");
    
    BMS_LOGI("Configuration saved successfully. Restarting in 3 seconds...");
    
    // Schedule restart after response is sent
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    
    return send_err;
}

// POST handler for canceling configuration mode
/// This function handles the cancellation of configuration mode by the user. This function performs following steps:
/// 1. Clears configuration mode flag in NVS to prevent re-entering config mode on next boot.
/// 2. Sends response indicating configuration was canceled with auto-redirect to main BMS page.
/// 3. Restarts the ESP32 to exit configuration mode and enter normal processing mode.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_config_cancel(httpd_req_t *req)
{
    BMS_LOGI("Configuration canceled by user");
    
    // Clear config mode flag
    nvs_handle_t nvs_handle;
    if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, "config_mode", 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        BMS_LOGI("Config mode flag cleared");
    }
    
    // Send response with redirect
    httpd_resp_set_type(req, "text/html");
    esp_err_t send_err = send_file(req, "/spiffs/bms/config_canceled.html", "text/html");
    
    BMS_LOGI("Restarting ESP32 to exit config mode...");
    
    // Restart to exit CONFIG mode and enter PROCESSING
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    
    return send_err;
}

/// This is the GET handler for root endpoint that redirects to main BMS page. It sends HTTP 302 redirect response.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_root_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/bms");
    return httpd_resp_send(req, NULL, 0);
}

/// This is the GET handler for main BMS page. It serves the index.html file.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_index(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/index.html", "text/html");
}

/// This is the GET handler for statistics page. It serves the stats.html file.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_stats_page(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/stats.html", "text/html");
}

/// This is the GET handler for configuration page. It serves the config.html file and
/// sets the configuration mode flag in NVS to indicate that device is in configuration mode.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_config_page(httpd_req_t *req)
{
    // Set config mode flag when user accesses config page
    nvs_handle_t nvs_handle;
    if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, "config_mode", 1);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        BMS_LOGI("Config mode activated via page access");
    }
    
    return send_file(req, "/spiffs/bms/config.html", "text/html");
}

/// This is the GET handler for serving JavaScript file used by charts on stats page. It serves the charts.js file.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_js_charts(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/js/charts.js", "application/javascript");
}

/// This is the GET handler for serving CSS stylesheet used by BMS web pages. It serves the style.css file.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_css_style(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/css/style.css", "text/css");
}

/// This is the GET handler for retrieving historical statistics data as JSON array response. It calls
/// function from stats history module to build and send the JSON data to the HTTP client.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_stats_data(httpd_req_t *req)
{
    return bms_stats_hist_send_as_json_array(req);
}

/// This fuction validates if the given string is a valid IPv4 address using inet_pton.
/// This uses the same validation mechanism as WiFi connection creation.
///
/// \param ip_str String representation of the IP address
/// \return true if the IP address is valid, false otherwise
static bool is_valid_ip(const char *ip_str)
{
    if (ip_str == NULL || strlen(ip_str) == 0) {
        return false;
    }

    struct in_addr addr;
    // inet_pton returns 1 on success, 0 if invalid format, -1 on error
    return inet_pton(AF_INET, ip_str, &addr) == 1;
}
