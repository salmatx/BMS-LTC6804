#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "bms_configuration.h"
#include "network_configuration.h"

typedef struct {
    wifi_cfg_t     wifi;
    mqtt_cfg_t     mqtt;
    bms_config_t   battery;
} configuration_t;

// Global runtime configuration (defaults from Kconfig/compiled defaults, then file overrides).
extern configuration_t g_cfg;

// Load JSON from SPIFFS and override fields present.
esp_err_t configuration_load(const char *path);
