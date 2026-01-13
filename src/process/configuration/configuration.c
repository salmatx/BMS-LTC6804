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
