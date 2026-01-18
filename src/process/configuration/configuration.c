#include "configuration.h"

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "cJSON.h"

#define LOG_MODULE_TAG "CFG"

configuration_t g_cfg = {
    .wifi = {
        .ssid = CONFIG_BMS_WIFI_SSID,
        .pass = CONFIG_BMS_WIFI_PASS,
    },
    .mqtt = {
        .uri  = CONFIG_BMS_MQTT_BROKER_URI,
    },
    .battery = {
        .cell_v_min   = 0.5f,
        .cell_v_max   = 2.0f,
        .pack_v_min   = 2.5f,
        .pack_v_max   = 10.0f,
        .current_min  = -5.0f,
        .current_max  = 5.0f
    }
};

static void json_get_str(cJSON *obj, const char *key, char *out, size_t out_sz)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(it) && it->valuestring) {
        snprintf(out, out_sz, "%s", it->valuestring);
    }
}

static void json_get_float(cJSON *obj, const char *key, float *out)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(it)) {
        *out = (float)it->valuedouble;
    }
}

esp_err_t configuration_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(LOG_MODULE_TAG, "Config %s not found, using defaults", path);
        return ESP_ERR_NOT_FOUND;
    }

    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return ESP_FAIL;
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return ESP_FAIL;

    cJSON *jwifi = cJSON_GetObjectItem(root, "wifi");
    if (cJSON_IsObject(jwifi)) {
        json_get_str(jwifi, "ssid", g_cfg.wifi.ssid, sizeof(g_cfg.wifi.ssid));
        json_get_str(jwifi, "pass", g_cfg.wifi.pass, sizeof(g_cfg.wifi.pass));
    }

    cJSON *jmqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(jmqtt)) {
        json_get_str(jmqtt, "uri", g_cfg.mqtt.uri, sizeof(g_cfg.mqtt.uri));
    }

    cJSON *jbatt = cJSON_GetObjectItem(root, "battery");
    if (cJSON_IsObject(jbatt)) {
        json_get_float(jbatt, "cell_v_min",  &g_cfg.battery.cell_v_min);
        json_get_float(jbatt, "cell_v_max",  &g_cfg.battery.cell_v_max);
        json_get_float(jbatt, "pack_v_min",  &g_cfg.battery.pack_v_min);
        json_get_float(jbatt, "pack_v_max",  &g_cfg.battery.pack_v_max);
        json_get_float(jbatt, "current_min", &g_cfg.battery.current_min);
        json_get_float(jbatt, "current_max", &g_cfg.battery.current_max);
    }

    cJSON_Delete(root);
    ESP_LOGI(LOG_MODULE_TAG, "Config loaded from %s", path);
    return ESP_OK;
}

esp_err_t configuration_save(const char *path)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_FAIL;

    // WiFi configuration
    cJSON *jwifi = cJSON_CreateObject();
    cJSON_AddStringToObject(jwifi, "ssid", g_cfg.wifi.ssid);
    cJSON_AddStringToObject(jwifi, "pass", g_cfg.wifi.pass);
    cJSON_AddItemToObject(root, "wifi", jwifi);

    // MQTT configuration
    cJSON *jmqtt = cJSON_CreateObject();
    cJSON_AddStringToObject(jmqtt, "uri", g_cfg.mqtt.uri);
    cJSON_AddItemToObject(root, "mqtt", jmqtt);

    // Battery configuration
    cJSON *jbatt = cJSON_CreateObject();
    cJSON_AddNumberToObject(jbatt, "cell_v_min", g_cfg.battery.cell_v_min);
    cJSON_AddNumberToObject(jbatt, "cell_v_max", g_cfg.battery.cell_v_max);
    cJSON_AddNumberToObject(jbatt, "pack_v_min", g_cfg.battery.pack_v_min);
    cJSON_AddNumberToObject(jbatt, "pack_v_max", g_cfg.battery.pack_v_max);
    cJSON_AddNumberToObject(jbatt, "current_min", g_cfg.battery.current_min);
    cJSON_AddNumberToObject(jbatt, "current_max", g_cfg.battery.current_max);
    cJSON_AddItemToObject(root, "battery", jbatt);

    // Convert to formatted JSON string
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(LOG_MODULE_TAG, "Failed to create JSON string");
        return ESP_FAIL;
    }

    // Write to file
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(LOG_MODULE_TAG, "Failed to open %s for writing", path);
        free(json_str);
        return ESP_FAIL;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, f);
    fclose(f);
    free(json_str);

    if (written != len) {
        ESP_LOGE(LOG_MODULE_TAG, "Failed to write config file");
        return ESP_FAIL;
    }

    ESP_LOGI(LOG_MODULE_TAG, "Config saved to %s", path);
    return ESP_OK;
}
