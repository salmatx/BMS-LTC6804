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
#include "bms_data.h"
#include "cJSON.h"
#include "wifi.h"
#include "led_control.h"

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
static esp_err_t h_template_save(httpd_req_t *req);
static esp_err_t h_template_edit(httpd_req_t *req);
static esp_err_t h_template_delete(httpd_req_t *req);
static esp_err_t h_config_templates(httpd_req_t *req);
static esp_err_t h_root_redirect(httpd_req_t *req);
static esp_err_t h_index(httpd_req_t *req);
static esp_err_t h_stats_page(httpd_req_t *req);
static esp_err_t h_config_page(httpd_req_t *req);
static esp_err_t h_js_charts(httpd_req_t *req);
static esp_err_t h_js_chartlib(httpd_req_t *req);
static esp_err_t h_js_batteries(httpd_req_t *req);
static esp_err_t h_css_style(httpd_req_t *req);
static esp_err_t h_stats_data(httpd_req_t *req);
static esp_err_t h_led_on(httpd_req_t *req);
static esp_err_t h_led_off(httpd_req_t *req);
static esp_err_t h_led_status(httpd_req_t *req);

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

    // Initialize LED control
    led_control_init();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.core_id = 0;
    cfg.stack_size = 8192;
    cfg.task_priority = 4;
    cfg.max_uri_handlers = 20;

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        s_httpd = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t u_root          = { .uri = "/",                     .method = HTTP_GET,  .handler = h_root_redirect };
    httpd_uri_t u_bms           = { .uri = "/bms",                  .method = HTTP_GET,  .handler = h_index };
    httpd_uri_t u_stats         = { .uri = "/bms/stats",            .method = HTTP_GET,  .handler = h_stats_page };
    httpd_uri_t u_cfg           = { .uri = "/bms/config",           .method = HTTP_GET,  .handler = h_config_page };
    httpd_uri_t u_js            = { .uri = "/bms/js/charts.js",     .method = HTTP_GET,  .handler = h_js_charts };
    httpd_uri_t u_js_chartlib   = { .uri = "/bms/js/chart.min.js",   .method = HTTP_GET,  .handler = h_js_chartlib };
    httpd_uri_t u_js_batt       = { .uri = "/bms/js/batteries.js",   .method = HTTP_GET,  .handler = h_js_batteries };
    httpd_uri_t u_data          = { .uri = "/bms/stats/data",       .method = HTTP_GET,  .handler = h_stats_data };
    httpd_uri_t u_cfg_data      = { .uri = "/bms/config/data",      .method = HTTP_GET,  .handler = h_config_data };
    httpd_uri_t u_cfg_save      = { .uri = "/bms/config/save",      .method = HTTP_POST, .handler = h_config_save };
    httpd_uri_t u_cfg_cancel    = { .uri = "/bms/config/cancel",    .method = HTTP_POST, .handler = h_config_cancel };
    httpd_uri_t u_tpl_save      = { .uri = "/bms/config/template/save", .method = HTTP_POST, .handler = h_template_save };
    httpd_uri_t u_tpl_edit      = { .uri = "/bms/config/template/edit", .method = HTTP_POST, .handler = h_template_edit };
    httpd_uri_t u_tpl_del       = { .uri = "/bms/config/template/delete", .method = HTTP_POST, .handler = h_template_delete };
    httpd_uri_t u_cfg_tpl       = { .uri = "/bms/config/templates",  .method = HTTP_GET,  .handler = h_config_templates };
    httpd_uri_t u_css           = { .uri = "/bms/css/style.css",    .method = HTTP_GET,  .handler = h_css_style };
    httpd_uri_t u_led_on        = { .uri = "/bms/led/on",           .method = HTTP_POST, .handler = h_led_on };
    httpd_uri_t u_led_off       = { .uri = "/bms/led/off",          .method = HTTP_POST, .handler = h_led_off };
    httpd_uri_t u_led_status    = { .uri = "/bms/led/status",       .method = HTTP_GET,  .handler = h_led_status };
    
    httpd_register_uri_handler(s_httpd, &u_root);
    httpd_register_uri_handler(s_httpd, &u_bms);
    httpd_register_uri_handler(s_httpd, &u_stats);
    httpd_register_uri_handler(s_httpd, &u_cfg);
    httpd_register_uri_handler(s_httpd, &u_js);
    httpd_register_uri_handler(s_httpd, &u_js_chartlib);
    httpd_register_uri_handler(s_httpd, &u_js_batt);
    httpd_register_uri_handler(s_httpd, &u_data);
    httpd_register_uri_handler(s_httpd, &u_cfg_data);
    httpd_register_uri_handler(s_httpd, &u_cfg_save);
    httpd_register_uri_handler(s_httpd, &u_cfg_cancel);
    httpd_register_uri_handler(s_httpd, &u_tpl_save);
    httpd_register_uri_handler(s_httpd, &u_tpl_edit);
    httpd_register_uri_handler(s_httpd, &u_tpl_del);
    httpd_register_uri_handler(s_httpd, &u_cfg_tpl);
    httpd_register_uri_handler(s_httpd, &u_css);
    httpd_register_uri_handler(s_httpd, &u_led_on);
    httpd_register_uri_handler(s_httpd, &u_led_off);
    httpd_register_uri_handler(s_httpd, &u_led_status);

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
    cJSON_AddStringToObject(wifi, "pass", g_cfg.wifi.pass);
    cJSON_AddBoolToObject(wifi, "no_pass", g_cfg.wifi.pass[0] == '\0');
    cJSON_AddStringToObject(wifi, "static_ip", g_cfg.wifi.static_ip);
    cJSON_AddStringToObject(wifi, "gateway", g_cfg.wifi.gateway);
    cJSON_AddStringToObject(wifi, "netmask", g_cfg.wifi.netmask);

    cJSON_AddItemToObject(root, "mqtt", mqtt);
    cJSON_AddStringToObject(mqtt, "uri", g_cfg.mqtt.uri);

    cJSON_AddItemToObject(root, "battery", bat);
    cJSON_AddStringToObject(bat, "adapter",
        g_cfg.battery.adapter_mode == BMS_ADAPTER_DEMO ? "demo" : "ltc6804");
    cJSON_AddNumberToObject(bat, "num_cells", g_cfg.battery.num_cells);
    cJSON_AddBoolToObject(bat, "current_enable", g_cfg.battery.current_enable);
    cJSON_AddBoolToObject(bat, "temperature_enable", g_cfg.battery.temperature_enable);
    cJSON_AddNumberToObject(bat, "cell_v_min", g_cfg.battery.cell_v_min);
    cJSON_AddNumberToObject(bat, "cell_v_max", g_cfg.battery.cell_v_max);
    cJSON_AddNumberToObject(bat, "pack_v_min", g_cfg.battery.pack_v_min);
    cJSON_AddNumberToObject(bat, "pack_v_max", g_cfg.battery.pack_v_max);
    cJSON_AddNumberToObject(bat, "series_pack_i_min", g_cfg.battery.series_pack_i_min);
    cJSON_AddNumberToObject(bat, "series_pack_i_max", g_cfg.battery.series_pack_i_max);

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

    // Check if "no password" checkbox was checked
    bool no_pass = false;
    if (parse_post_param(buf, "wifi_no_pass", value, sizeof(value)) == ESP_OK) {
        no_pass = (strcmp(value, "1") == 0 || strcmp(value, "on") == 0);
    }

    if (no_pass) {
        BMS_LOGI("No-password mode selected, clearing WiFi password");
        g_cfg.wifi.pass[0] = '\0';
    } else if (parse_post_param(buf, "wifi_pass", value, sizeof(value)) == ESP_OK) {
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
    
    // Parse adapter mode
    if (parse_post_param(buf, "adapter", value, sizeof(value)) == ESP_OK) {
        if (strcmp(value, "demo") == 0) {
            g_cfg.battery.adapter_mode = BMS_ADAPTER_DEMO;
        } else {
            g_cfg.battery.adapter_mode = BMS_ADAPTER_LTC6804;
        }
    }

    // Parse battery parameters and round to 2 decimal places
    if (parse_post_param(buf, "num_cells", value, sizeof(value)) == ESP_OK) {
        int nc = atoi(value);
        if (nc < 1) nc = 1;
        if (nc > BMS_MAX_CELLS) nc = BMS_MAX_CELLS;
        g_cfg.battery.num_cells = (uint8_t)nc;
    }
    if (parse_post_param(buf, "current_enable", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.current_enable = (strcmp(value, "1") == 0 || strcmp(value, "on") == 0 || strcmp(value, "true") == 0);
    } else {
        // Checkbox not present in POST data means unchecked
        g_cfg.battery.current_enable = false;
    }
    if (parse_post_param(buf, "temperature_enable", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.temperature_enable = (strcmp(value, "1") == 0 || strcmp(value, "on") == 0 || strcmp(value, "true") == 0);
    } else {
        // Checkbox not present in POST data means unchecked
        g_cfg.battery.temperature_enable = false;
    }
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
    if (parse_post_param(buf, "series_pack_i_min", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.series_pack_i_min = roundf(atof(value) * 100.0f) / 100.0f;
    }
    if (parse_post_param(buf, "series_pack_i_max", value, sizeof(value)) == ESP_OK) {
        g_cfg.battery.series_pack_i_max = roundf(atof(value) * 100.0f) / 100.0f;
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

/// This is the GET handler for serving battery templates JSON.
/// Serves the cached templates string directly without cJSON parsing to minimize heap usage.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_config_templates(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    const char *tpl = configuration_get_battery_templates_json();
    return httpd_resp_sendstr(req, tpl ? tpl : "[]");
}

/// This is the POST handler for saving a custom battery template. It receives a JSON body with
/// battery parameters and adds the template to the battery_templates array stored in config.json.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_template_save(httpd_req_t *req)
{
    char buf[512];
    int remaining = req->content_len;

    if (remaining >= (int)sizeof(buf)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Content too long\"}");
    }

    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    BMS_LOGI("Received template save request: %s", buf);

    cJSON *body = cJSON_Parse(buf);
    if (!body) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    }

    cJSON *jid   = cJSON_GetObjectItem(body, "id");
    cJSON *jname = cJSON_GetObjectItem(body, "name");
    cJSON *jcat  = cJSON_GetObjectItem(body, "category");

    if (!cJSON_IsString(jid) || !cJSON_IsString(jname) || !cJSON_IsString(jcat) ||
        strlen(jname->valuestring) == 0 || strlen(jcat->valuestring) == 0) {
        cJSON_Delete(body);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Missing required fields\"}");
    }

    float cell_v_min  = 0, cell_v_max = 0, series_pack_i_min = 0, series_pack_i_max = 0;
    cJSON *jval;
    jval = cJSON_GetObjectItem(body, "cell_v_min");  if (cJSON_IsNumber(jval)) cell_v_min  = (float)jval->valuedouble;
    jval = cJSON_GetObjectItem(body, "cell_v_max");  if (cJSON_IsNumber(jval)) cell_v_max  = (float)jval->valuedouble;
    jval = cJSON_GetObjectItem(body, "series_pack_i_min"); if (cJSON_IsNumber(jval)) series_pack_i_min = (float)jval->valuedouble;
    jval = cJSON_GetObjectItem(body, "series_pack_i_max"); if (cJSON_IsNumber(jval)) series_pack_i_max = (float)jval->valuedouble;

    // Copy strings before freeing body to reduce peak heap usage during save
    char id_buf[64] = {0}, name_buf[64] = {0}, cat_buf[64] = {0};
    strncpy(id_buf,   jid->valuestring,   sizeof(id_buf) - 1);
    strncpy(name_buf, jname->valuestring, sizeof(name_buf) - 1);
    strncpy(cat_buf,  jcat->valuestring,  sizeof(cat_buf) - 1);
    cJSON_Delete(body);

    esp_err_t err = configuration_add_battery_template(
        id_buf, name_buf, cat_buf,
        cell_v_min, cell_v_max, series_pack_i_min, series_pack_i_max);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Failed to save template\"}");
    }
}

/// This is the POST handler for editing an existing battery template. It receives a JSON body
/// with the template id and updated parameters, then replaces the template in config.json.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_template_edit(httpd_req_t *req)
{
    char buf[512];
    int remaining = req->content_len;

    if (remaining >= (int)sizeof(buf)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Content too long\"}");
    }

    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    BMS_LOGI("Received template edit request: %s", buf);

    cJSON *body = cJSON_Parse(buf);
    if (!body) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    }

    cJSON *jid   = cJSON_GetObjectItem(body, "id");
    cJSON *jname = cJSON_GetObjectItem(body, "name");
    cJSON *jcat  = cJSON_GetObjectItem(body, "category");

    if (!cJSON_IsString(jid) || !cJSON_IsString(jname) || !cJSON_IsString(jcat) ||
        strlen(jid->valuestring) == 0 || strlen(jname->valuestring) == 0 || strlen(jcat->valuestring) == 0) {
        cJSON_Delete(body);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Missing required fields\"}");
    }

    float cell_v_min = 0, cell_v_max = 0, series_pack_i_min = 0, series_pack_i_max = 0;
    cJSON *jval;
    jval = cJSON_GetObjectItem(body, "cell_v_min");  if (cJSON_IsNumber(jval)) cell_v_min  = (float)jval->valuedouble;
    jval = cJSON_GetObjectItem(body, "cell_v_max");  if (cJSON_IsNumber(jval)) cell_v_max  = (float)jval->valuedouble;
    jval = cJSON_GetObjectItem(body, "series_pack_i_min"); if (cJSON_IsNumber(jval)) series_pack_i_min = (float)jval->valuedouble;
    jval = cJSON_GetObjectItem(body, "series_pack_i_max"); if (cJSON_IsNumber(jval)) series_pack_i_max = (float)jval->valuedouble;

    char id_buf[64] = {0}, name_buf[64] = {0}, cat_buf[64] = {0};
    strncpy(id_buf,   jid->valuestring,   sizeof(id_buf) - 1);
    strncpy(name_buf, jname->valuestring, sizeof(name_buf) - 1);
    strncpy(cat_buf,  jcat->valuestring,  sizeof(cat_buf) - 1);
    cJSON_Delete(body);

    esp_err_t err = configuration_edit_battery_template(
        id_buf, name_buf, cat_buf,
        cell_v_min, cell_v_max, series_pack_i_min, series_pack_i_max);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Failed to edit template\"}");
    }
}

/// This is the POST handler for deleting a battery template. It receives a JSON body
/// with the template id and removes it from the battery_templates array in config.json.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_template_delete(httpd_req_t *req)
{
    char buf[256];
    int remaining = req->content_len;

    if (remaining >= (int)sizeof(buf)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Content too long\"}");
    }

    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    BMS_LOGI("Received template delete request: %s", buf);

    cJSON *body = cJSON_Parse(buf);
    if (!body) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    }

    cJSON *jid = cJSON_GetObjectItem(body, "id");
    if (!cJSON_IsString(jid) || strlen(jid->valuestring) == 0) {
        cJSON_Delete(body);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Missing id\"}");
    }

    char id_buf[64] = {0};
    strncpy(id_buf, jid->valuestring, sizeof(id_buf) - 1);
    cJSON_Delete(body);

    esp_err_t err = configuration_delete_battery_template(id_buf);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Failed to delete template\"}");
    }
}

/// This is the GET handler for root endpoint that redirects to main BMS page.
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

/// This is the GET handler for serving the bundled Chart.js library.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_js_chartlib(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/js/chart.min.js", "application/javascript");
}

/// This is the GET handler for serving battery templates JavaScript file. It serves the batteries.js file.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_js_batteries(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/js/batteries.js", "application/javascript");
}

/// This is the GET handler for serving CSS stylesheet used by BMS web pages. It serves the style.css file.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_css_style(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/css/style.css", "text/css");
}

/// This is the GET handler for retrieving the latest statistics sample as a single JSON object.
/// History is maintained on the browser side.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t h_stats_data(httpd_req_t *req)
{
    return bms_stats_hist_send_latest(req);
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

/// POST handler for turning LED on. Calls LED control module.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success
static esp_err_t h_led_on(httpd_req_t *req)
{
    led_control_turn_on();
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"on\"}");
}

/// POST handler for turning LED off. Calls LED control module.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success
static esp_err_t h_led_off(httpd_req_t *req)
{
    led_control_turn_off();
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"off\"}");
}

/// GET handler for retrieving current LED status.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success
static esp_err_t h_led_status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    bool led_on = led_control_get_state();
    const char *status = led_on ? "{\"status\":\"on\"}" : "{\"status\":\"off\"}";
    return httpd_resp_sendstr(req, status);
}
