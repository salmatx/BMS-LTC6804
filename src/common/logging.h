/// Header file for `logging.c`.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once

#include "esp_log.h"

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/
/// Maximum length of a single error log entry stored in RTC memory
#define BMS_LOG_ENTRY_MAXLEN  128
/// Number of error log entries stored in RTC ring buffer
#define BMS_LOG_ENTRY_COUNT   5

/// Deffault log module tag. Can be overridden by defining LOG_MODULE_TAG after including this header.
#ifndef LOG_MODULE_TAG
#define LOG_MODULE_TAG "BMS"
#endif

/// Error log macro used to print error messages to stdout and store in RTC ring buffer.
#define BMS_LOGE(fmt, ...) do { \
    ESP_LOGE(LOG_MODULE_TAG, fmt, ##__VA_ARGS__); \
    bms_log_rtc_store(LOG_MODULE_TAG, fmt, ##__VA_ARGS__); \
} while(0)
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
void bms_logging_init(void);
void bms_logging_set_global_level(esp_log_level_t level);

void bms_log_rtc_store(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void bms_log_rtc_get_entries(char out[][BMS_LOG_ENTRY_MAXLEN], int *count);
void bms_log_rtc_clear(void);
void bms_logging_set_module_level(const char *module_tag, esp_log_level_t level);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/