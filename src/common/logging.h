/// Header file for `logging.c`.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once

#include "esp_log.h"

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/
/// Deffault log module tag. Can be overridden by defining LOG_MODULE_TAG after including this header.
#ifndef LOG_MODULE_TAG
#define LOG_MODULE_TAG "BMS"
#endif

/// Error log macro used to print error messages to stdout.
#define BMS_LOGE(fmt, ...) ESP_LOGE(LOG_MODULE_TAG, fmt, ##__VA_ARGS__)
/// Warning log macro used to print warning messages to stdout.
#define BMS_LOGW(fmt, ...) ESP_LOGW(LOG_MODULE_TAG, fmt, ##__VA_ARGS__)
/// Info log macro used to print informational messages to stdout.
#define BMS_LOGI(fmt, ...) ESP_LOGI(LOG_MODULE_TAG, fmt, ##__VA_ARGS__)
/// Debug log macro used to print debug messages to stdout.
#define BMS_LOGD(fmt, ...) ESP_LOGD(LOG_MODULE_TAG, fmt, ##__VA_ARGS__)
/// Verbose log macro used to print verbose messages to stdout.
#define BMS_LOGV(fmt, ...) ESP_LOGV(LOG_MODULE_TAG, fmt, ##__VA_ARGS__)

/// System-wide log tag for messages not specific to any module.
#define BMS_LOG_TAG "BMS"

/// System-wide error log macro used to print error messages to stdout.
#define BMS_SYS_E(fmt, ...) ESP_LOGE(BMS_LOG_TAG, fmt, ##__VA_ARGS__)
/// System-wide warning log macro used to print warning messages to stdout.
#define BMS_SYS_W(fmt, ...) ESP_LOGW(BMS_LOG_TAG, fmt, ##__VA_ARGS__)
/// System-wide info log macro used to print informational messages to stdout.
#define BMS_SYS_I(fmt, ...) ESP_LOGI(BMS_LOG_TAG, fmt, ##__VA_ARGS__)
/// System-wide debug log macro used to print debug messages to stdout.
#define BMS_SYS_D(fmt, ...) ESP_LOGD(BMS_LOG_TAG, fmt, ##__VA_ARGS__)
/// System-wide verbose log macro used to print verbose messages to stdout.
#define BMS_SYS_V(fmt, ...) ESP_LOGV(BMS_LOG_TAG, fmt, ##__VA_ARGS__)

/*==============================================================================================================*/
/*                                               Public Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Variables                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                        Public Function Prototypes                                            */
/*==============================================================================================================*/
// Initialization API
void bms_logging_init(void);
void bms_logging_set_global_level(esp_log_level_t level);
void bms_logging_set_module_level(const char *module_tag, esp_log_level_t level);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/