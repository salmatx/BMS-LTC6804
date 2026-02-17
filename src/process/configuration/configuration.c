/// This module implements configuration loading and saving.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "configuration.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "cJSON.h"
#include "bms_data.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "CFG"

/// Maximum allowed configuration file size in bytes (16 KB)
#define MAX_CONFIG_FILE_SIZE  16384

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static void json_get_str(cJSON *obj, const char *key, char *out, size_t out_sz);
static void json_get_float(cJSON *obj, const char *key, float *out);
static void json_get_int(cJSON *obj, const char *key, int *out);
static void json_get_bool(cJSON *obj, const char *key, bool *out);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Cached JSON string of battery_templates array from config file.
/// Preserved across load/save so that configuration_save does not lose template data.
static char *s_battery_templates_json = NULL;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/
/// Global configuration instance
configuration_t g_cfg = {
    .wifi = {
        .ssid = CONFIG_BMS_WIFI_SSID,
        .pass = CONFIG_BMS_WIFI_PASS,
        .static_ip = "",
        .gateway = "",
        .netmask = "",
    },
    .mqtt = {
        .uri  = CONFIG_BMS_MQTT_BROKER_URI,
    },
    .battery = {
        .num_cells      = 5,
        .current_enable = false,
        .cell_v_min     = 0.5f,
        .cell_v_max     = 2.0f,
        .pack_v_min     = 2.5f,
        .pack_v_max     = 10.0f,
        .current_min    = -5.0f,
        .current_max    = 5.0f
    }
};

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function loads the configuration from a JSON file at the specified path into the global configuration instance.
///
/// \param path Path to configuration file
/// \return ESP_OK on success, otherwise an error code
esp_err_t configuration_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(LOG_MODULE_TAG, "Config %s not found, using defaults", path);
        return ESP_ERR_NOT_FOUND;
    }

    // Determine file size for dynamic allocation
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > MAX_CONFIG_FILE_SIZE) {
        fclose(f);
        ESP_LOGE(LOG_MODULE_TAG, "Config file size invalid (%ld)", fsize);
        return ESP_FAIL;
    }

    char *buf = malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        ESP_LOGE(LOG_MODULE_TAG, "Failed to allocate config buffer");
        return ESP_FAIL;
    }

    size_t n = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    if (n == 0) {
        free(buf);
        return ESP_FAIL;
    }
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return ESP_FAIL;

    cJSON *jwifi = cJSON_GetObjectItem(root, "wifi");
    if (cJSON_IsObject(jwifi)) {
        json_get_str(jwifi, "ssid", g_cfg.wifi.ssid, sizeof(g_cfg.wifi.ssid));
        json_get_str(jwifi, "pass", g_cfg.wifi.pass, sizeof(g_cfg.wifi.pass));
        json_get_str(jwifi, "static_ip", g_cfg.wifi.static_ip, sizeof(g_cfg.wifi.static_ip));
        json_get_str(jwifi, "gateway", g_cfg.wifi.gateway, sizeof(g_cfg.wifi.gateway));
        json_get_str(jwifi, "netmask", g_cfg.wifi.netmask, sizeof(g_cfg.wifi.netmask));
    }

    cJSON *jmqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(jmqtt)) {
        json_get_str(jmqtt, "uri", g_cfg.mqtt.uri, sizeof(g_cfg.mqtt.uri));
    }

    cJSON *jbatt = cJSON_GetObjectItem(root, "battery");
    if (cJSON_IsObject(jbatt)) {
        int num_cells = (int)g_cfg.battery.num_cells;
        json_get_int(jbatt, "num_cells", &num_cells);
        if (num_cells < 1) num_cells = 1;
        if (num_cells > BMS_MAX_CELLS) num_cells = BMS_MAX_CELLS;
        g_cfg.battery.num_cells = (uint8_t)num_cells;
        json_get_bool(jbatt, "current_enable", &g_cfg.battery.current_enable);
        json_get_float(jbatt, "cell_v_min",  &g_cfg.battery.cell_v_min);
        json_get_float(jbatt, "cell_v_max",  &g_cfg.battery.cell_v_max);
        json_get_float(jbatt, "pack_v_min",  &g_cfg.battery.pack_v_min);
        json_get_float(jbatt, "pack_v_max",  &g_cfg.battery.pack_v_max);
        json_get_float(jbatt, "current_min", &g_cfg.battery.current_min);
        json_get_float(jbatt, "current_max", &g_cfg.battery.current_max);
    }

    // Cache battery_templates JSON for later use (web UI and re-saving)
    cJSON *jtemplates = cJSON_GetObjectItem(root, "battery_templates");
    if (cJSON_IsArray(jtemplates)) {
        free(s_battery_templates_json);
        s_battery_templates_json = cJSON_PrintUnformatted(jtemplates);
        if (s_battery_templates_json) {
            ESP_LOGI(LOG_MODULE_TAG, "Battery templates loaded (%u bytes)",
                     (unsigned)strlen(s_battery_templates_json));
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(LOG_MODULE_TAG, "Config loaded from %s", path);
    return ESP_OK;
}

/// This function saves the global configuration instance to a JSON file at the specified path.
///
/// \param path Path to configuration file
/// \return ESP_OK on success, otherwise an error code
esp_err_t configuration_save(const char *path)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_FAIL;

    // WiFi configuration
    cJSON *jwifi = cJSON_CreateObject();
    cJSON_AddStringToObject(jwifi, "ssid", g_cfg.wifi.ssid);
    cJSON_AddStringToObject(jwifi, "pass", g_cfg.wifi.pass);
    cJSON_AddStringToObject(jwifi, "static_ip", g_cfg.wifi.static_ip);
    cJSON_AddStringToObject(jwifi, "gateway", g_cfg.wifi.gateway);
    cJSON_AddStringToObject(jwifi, "netmask", g_cfg.wifi.netmask);
    cJSON_AddItemToObject(root, "wifi", jwifi);

    // MQTT configuration
    cJSON *jmqtt = cJSON_CreateObject();
    cJSON_AddStringToObject(jmqtt, "uri", g_cfg.mqtt.uri);
    cJSON_AddItemToObject(root, "mqtt", jmqtt);

    // Battery configuration
    cJSON *jbatt = cJSON_CreateObject();
    cJSON_AddNumberToObject(jbatt, "num_cells", g_cfg.battery.num_cells);
    cJSON_AddBoolToObject(jbatt, "current_enable", g_cfg.battery.current_enable);
    cJSON_AddNumberToObject(jbatt, "cell_v_min", g_cfg.battery.cell_v_min);
    cJSON_AddNumberToObject(jbatt, "cell_v_max", g_cfg.battery.cell_v_max);
    cJSON_AddNumberToObject(jbatt, "pack_v_min", g_cfg.battery.pack_v_min);
    cJSON_AddNumberToObject(jbatt, "pack_v_max", g_cfg.battery.pack_v_max);
    cJSON_AddNumberToObject(jbatt, "current_min", g_cfg.battery.current_min);
    cJSON_AddNumberToObject(jbatt, "current_max", g_cfg.battery.current_max);
    cJSON_AddItemToObject(root, "battery", jbatt);

    // Re-attach battery_templates if previously loaded
    if (s_battery_templates_json) {
        cJSON *jtemplates = cJSON_Parse(s_battery_templates_json);
        if (jtemplates) {
            cJSON_AddItemToObject(root, "battery_templates", jtemplates);
        }
    }

    // Convert to compact JSON string (unformatted to reduce heap usage on ESP32)
    char *json_str = cJSON_PrintUnformatted(root);
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

/// This function returns the cached battery templates JSON string loaded from config file.
/// The returned string is a JSON array. Returns NULL if no templates were loaded.
///
/// \param None
/// \return Pointer to battery templates JSON string, or NULL
const char *configuration_get_battery_templates_json(void)
{
    return s_battery_templates_json;
}

/// This function adds a new custom battery template to the cached templates and saves to config file.
/// If a group with the given category label already exists, the battery is appended to that group.
/// Otherwise, a new group is created.
///
/// \param id        Unique identifier for the battery (e.g. "custom_1")
/// \param name      Display name for the battery
/// \param category  Group label (e.g. "Li-ion NMC/NCA")
/// \param cell_v_min  Minimum cell voltage
/// \param cell_v_max  Maximum cell voltage
/// \param current_min Minimum current (discharge, negative)
/// \param current_max Maximum current (charge, positive)
/// \return ESP_OK on success, otherwise an error code
esp_err_t configuration_add_battery_template(const char *id, const char *name, const char *category,
                                              float cell_v_min, float cell_v_max,
                                              float current_min, float current_max)
{
    // Parse existing templates or create empty array
    cJSON *templates = NULL;
    if (s_battery_templates_json) {
        templates = cJSON_Parse(s_battery_templates_json);
    }
    if (!templates) {
        templates = cJSON_CreateArray();
    }

    // Find existing group by label
    cJSON *target_group = NULL;
    cJSON *group = NULL;
    cJSON_ArrayForEach(group, templates) {
        cJSON *jlabel = cJSON_GetObjectItem(group, "label");
        if (cJSON_IsString(jlabel) && strcmp(jlabel->valuestring, category) == 0) {
            target_group = group;
            break;
        }
    }

    // Create group if not found
    if (!target_group) {
        target_group = cJSON_CreateObject();
        cJSON_AddStringToObject(target_group, "label", category);
        cJSON_AddItemToObject(target_group, "batteries", cJSON_CreateArray());
        cJSON_AddItemToArray(templates, target_group);
    }

    // Build new battery entry
    cJSON *battery = cJSON_CreateObject();
    cJSON_AddStringToObject(battery, "id", id);
    cJSON_AddStringToObject(battery, "name", name);
    cJSON_AddNumberToObject(battery, "cell_v_min", cell_v_min);
    cJSON_AddNumberToObject(battery, "cell_v_max", cell_v_max);
    cJSON_AddNumberToObject(battery, "current_min", current_min);
    cJSON_AddNumberToObject(battery, "current_max", current_max);

    cJSON *batteries_arr = cJSON_GetObjectItem(target_group, "batteries");
    if (!cJSON_IsArray(batteries_arr)) {
        batteries_arr = cJSON_CreateArray();
        cJSON_AddItemToObject(target_group, "batteries", batteries_arr);
    }
    cJSON_AddItemToArray(batteries_arr, battery);

    // Re-serialize and update cache
    char *new_json = cJSON_PrintUnformatted(templates);
    cJSON_Delete(templates);

    if (!new_json) {
        ESP_LOGE(LOG_MODULE_TAG, "Failed to serialize updated templates");
        return ESP_FAIL;
    }

    free(s_battery_templates_json);
    s_battery_templates_json = new_json;

    ESP_LOGI(LOG_MODULE_TAG, "Battery template '%s' added to group '%s'", name, category);

    // Persist to config file
    return configuration_save("/spiffs/config.json");
}

/// This function edits an existing battery template identified by its id.
/// It first removes the old entry, then adds the updated one.
///
/// \param id        Unique identifier of the battery to edit
/// \param name      Updated display name
/// \param category  Group label (may differ from original to move between groups)
/// \param cell_v_min  Minimum cell voltage
/// \param cell_v_max  Maximum cell voltage
/// \param current_min Minimum current
/// \param current_max Maximum current
/// \return ESP_OK on success, otherwise an error code
esp_err_t configuration_edit_battery_template(const char *id, const char *name, const char *category,
                                              float cell_v_min, float cell_v_max,
                                              float current_min, float current_max)
{
    cJSON *templates = NULL;
    if (s_battery_templates_json) {
        templates = cJSON_Parse(s_battery_templates_json);
    }
    if (!templates) {
        ESP_LOGE(LOG_MODULE_TAG, "No templates to edit");
        return ESP_FAIL;
    }

    // Find and remove old entry by id across all groups
    bool found = false;
    cJSON *group = NULL;
    cJSON_ArrayForEach(group, templates) {
        cJSON *batteries = cJSON_GetObjectItem(group, "batteries");
        if (!cJSON_IsArray(batteries)) continue;
        int size = cJSON_GetArraySize(batteries);
        for (int i = 0; i < size; i++) {
            cJSON *b = cJSON_GetArrayItem(batteries, i);
            cJSON *bid = cJSON_GetObjectItem(b, "id");
            if (cJSON_IsString(bid) && strcmp(bid->valuestring, id) == 0) {
                cJSON_DeleteItemFromArray(batteries, i);
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        cJSON_Delete(templates);
        ESP_LOGW(LOG_MODULE_TAG, "Template '%s' not found for editing", id);
        return ESP_ERR_NOT_FOUND;
    }

    // Remove empty groups left behind
    int gsize = cJSON_GetArraySize(templates);
    for (int i = gsize - 1; i >= 0; i--) {
        cJSON *g = cJSON_GetArrayItem(templates, i);
        cJSON *barr = cJSON_GetObjectItem(g, "batteries");
        if (cJSON_IsArray(barr) && cJSON_GetArraySize(barr) == 0) {
            cJSON_DeleteItemFromArray(templates, i);
        }
    }

    // Update cache with the entry removed
    char *tmp_json = cJSON_PrintUnformatted(templates);
    cJSON_Delete(templates);
    if (!tmp_json) {
        ESP_LOGE(LOG_MODULE_TAG, "Failed to serialize templates after removal");
        return ESP_FAIL;
    }
    free(s_battery_templates_json);
    s_battery_templates_json = tmp_json;

    // Add updated entry (reuses existing add logic which also saves to file)
    ESP_LOGI(LOG_MODULE_TAG, "Editing template '%s' in group '%s'", name, category);
    return configuration_add_battery_template(id, name, category,
                                               cell_v_min, cell_v_max, current_min, current_max);
}

/// This function removes a battery template by its id from the cached templates and saves to config file.
/// If the removal leaves an empty group, that group is also removed.
///
/// \param id Unique identifier of the battery to remove
/// \return ESP_OK on success, otherwise an error code
esp_err_t configuration_delete_battery_template(const char *id)
{
    cJSON *templates = NULL;
    if (s_battery_templates_json) {
        templates = cJSON_Parse(s_battery_templates_json);
    }
    if (!templates) {
        ESP_LOGE(LOG_MODULE_TAG, "No templates to delete from");
        return ESP_FAIL;
    }

    bool found = false;
    cJSON *group = NULL;
    cJSON_ArrayForEach(group, templates) {
        cJSON *batteries = cJSON_GetObjectItem(group, "batteries");
        if (!cJSON_IsArray(batteries)) continue;
        int size = cJSON_GetArraySize(batteries);
        for (int i = 0; i < size; i++) {
            cJSON *b = cJSON_GetArrayItem(batteries, i);
            cJSON *bid = cJSON_GetObjectItem(b, "id");
            if (cJSON_IsString(bid) && strcmp(bid->valuestring, id) == 0) {
                cJSON_DeleteItemFromArray(batteries, i);
                found = true;
                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        cJSON_Delete(templates);
        ESP_LOGW(LOG_MODULE_TAG, "Template '%s' not found for deletion", id);
        return ESP_ERR_NOT_FOUND;
    }

    // Remove empty groups
    int gsize = cJSON_GetArraySize(templates);
    for (int i = gsize - 1; i >= 0; i--) {
        cJSON *g = cJSON_GetArrayItem(templates, i);
        cJSON *barr = cJSON_GetObjectItem(g, "batteries");
        if (cJSON_IsArray(barr) && cJSON_GetArraySize(barr) == 0) {
            cJSON_DeleteItemFromArray(templates, i);
        }
    }

    char *new_json = cJSON_PrintUnformatted(templates);
    cJSON_Delete(templates);
    if (!new_json) {
        ESP_LOGE(LOG_MODULE_TAG, "Failed to serialize templates after delete");
        return ESP_FAIL;
    }

    free(s_battery_templates_json);
    s_battery_templates_json = new_json;

    ESP_LOGI(LOG_MODULE_TAG, "Battery template '%s' deleted", id);
    return configuration_save("/spiffs/config.json");
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// This helper function retrieves a string value from a cJSON object by key.
///
/// \param obj Pointer to cJSON object
/// \param key Key of the string item
/// \param out Output buffer for the string
/// \param out_sz Size of the output buffer
/// \return None
static void json_get_str(cJSON *obj, const char *key, char *out, size_t out_sz)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(it) && it->valuestring) {
        snprintf(out, out_sz, "%s", it->valuestring);
    }
}

/// This helper function retrieves a float value from a cJSON object by key.
///
/// \param obj Pointer to cJSON object
/// \param key Key of the number item
/// \param out Pointer to output float variable
/// \return None
static void json_get_float(cJSON *obj, const char *key, float *out)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(it)) {
        *out = (float)it->valuedouble;
    }
}

/// This helper function retrieves an integer value from a cJSON object by key.
///
/// \param obj Pointer to cJSON object
/// \param key Key of the number item
/// \param out Pointer to output int variable
/// \return None
static void json_get_int(cJSON *obj, const char *key, int *out)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(it)) {
        *out = it->valueint;
    }
}

/// This helper function retrieves a boolean value from a cJSON object by key.
///
/// \param obj Pointer to cJSON object
/// \param key Key of the boolean item
/// \param out Pointer to output bool variable
/// \return None
static void json_get_bool(cJSON *obj, const char *key, bool *out)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsBool(it)) {
        *out = cJSON_IsTrue(it) ? true : false;
    }
}