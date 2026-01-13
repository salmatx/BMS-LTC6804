#include "http_server.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "logging.h"
#include "stats_history.h"
#include "configuration.h"
#include "cJSON.h"

#define LOG_MODULE_TAG "BMS_HTTP"

static httpd_handle_t s_httpd = NULL;

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

static esp_err_t h_bms_config_data(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();
    cJSON *mqtt = cJSON_CreateObject();
    cJSON *bat  = cJSON_CreateObject();

    cJSON_AddItemToObject(root, "wifi", wifi);
    cJSON_AddStringToObject(wifi, "ssid", g_cfg.wifi.ssid);

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

    esp_err_t err = httpd_resp_sendstr(req, out);  // send string response
    free(out);
    return err;
}

// URI Handlers
static esp_err_t h_root_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/bms");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t h_bms_index(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/index.html", "text/html");
}

static esp_err_t h_bms_stats_page(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/stats.html", "text/html");
}

static esp_err_t h_bms_config_page(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/config.html", "text/html");
}

static esp_err_t h_bms_js_charts(httpd_req_t *req)
{
    return send_file(req, "/spiffs/bms/js/charts.js", "application/javascript");
}

// Dynamic data used by charts
static esp_err_t h_bms_stats_data(httpd_req_t *req)
{
    return bms_stats_hist_send_as_json_array(req);
}

esp_err_t bms_http_server_start(void)
{
    if (s_httpd) return ESP_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.core_id = 0;            /* Slow core */
    cfg.stack_size = 4096;
    cfg.task_priority = 4;

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        s_httpd = NULL;
        return ESP_FAIL;
    }

    httpd_uri_t u_root      = { .uri = "/",              .method = HTTP_GET, .handler = h_root_redirect };
    httpd_uri_t u_bms       = { .uri = "/bms",           .method = HTTP_GET, .handler = h_bms_index };
    httpd_uri_t u_stats     = { .uri = "/bms/stats",     .method = HTTP_GET, .handler = h_bms_stats_page };
    httpd_uri_t u_cfg       = { .uri = "/bms/config",    .method = HTTP_GET, .handler = h_bms_config_page };
    httpd_uri_t u_js        = { .uri = "/bms/js/charts.js", .method = HTTP_GET, .handler = h_bms_js_charts };
    httpd_uri_t u_data      = { .uri = "/bms/stats/data", .method = HTTP_GET, .handler = h_bms_stats_data };
    httpd_uri_t u_cfg_data  = { .uri = "/bms/config/data", .method = HTTP_GET, .handler = h_bms_config_data };
    
    
    httpd_register_uri_handler(s_httpd, &u_root);
    httpd_register_uri_handler(s_httpd, &u_bms);
    httpd_register_uri_handler(s_httpd, &u_stats);
    httpd_register_uri_handler(s_httpd, &u_cfg);
    httpd_register_uri_handler(s_httpd, &u_js);
    httpd_register_uri_handler(s_httpd, &u_data);
    httpd_register_uri_handler(s_httpd, &u_cfg_data);

    BMS_LOGI("HTTP server started");
    return ESP_OK;
}

esp_err_t bms_http_server_stop(void)
{
    if (!s_httpd) return ESP_OK;
    httpd_stop(s_httpd);
    s_httpd = NULL;
    return ESP_OK;
}
