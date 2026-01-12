#include "spiffs.h"
#include "esp_spiffs.h"
#include "logging.h"

#define LOG_MODULE_TAG "BMS_SPIFFS"

esp_err_t bms_spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 8,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        BMS_LOGE("SPIFFS mount failed: %s", esp_err_to_name(err));
    }
    return err;
}
