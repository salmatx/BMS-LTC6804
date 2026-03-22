/// Header file for `configuration.c`.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "bms_configuration.h"
#include "network_configuration.h"

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                               Public Types                                                   */
/*==============================================================================================================*/
/// Structure defining the complete runtime configuration.
typedef struct {
    wifi_cfg_t     wifi;        ///< Wi-Fi configuration
    mqtt_cfg_t     mqtt;        ///< MQTT configuration
    bms_config_t   battery;     ///< Battery properties configuration
} configuration_t;

/*==============================================================================================================*/
/*                                             Public Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Variables                                                 */
/*==============================================================================================================*/
/// Global runtime configuration (initialized with default values from sdkconfig, then overridden from
/// configuration file in SPIFFS if present).
extern configuration_t g_cfg;

/*==============================================================================================================*/
/*                                        Public Function Prototypes                                            */
/*==============================================================================================================*/
esp_err_t configuration_load(const char *path);
esp_err_t configuration_save(const char *path);
const char *configuration_get_battery_templates_json(void);
esp_err_t configuration_add_battery_template(const char *id, const char *name, const char *category,
                                              float cell_v_min, float cell_v_max,
                                              float series_pack_i_min, float series_pack_i_max);
esp_err_t configuration_edit_battery_template(const char *id, const char *name, const char *category,
                                              float cell_v_min, float cell_v_max,
                                              float series_pack_i_min, float series_pack_i_max);
esp_err_t configuration_delete_battery_template(const char *id);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/
