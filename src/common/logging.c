/// This module implements logging initialization and configuration functions used for logging across the project.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"
#include "esp_system.h"
#include "esp_attr.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Magic value used to validate RTC ring buffer contents after reset.
/// On power-on reset the RTC NOINIT memory contains random data. By checking whether `magic` matches
/// this constant, ::bms_logging_init can distinguish a valid buffer left by a previous soft/watchdog
/// reset (preserve entries) from an uninitialised buffer after a cold boot (clear entries).
/// The specific value is arbitrary; it is chosen to be recognisable in a memory dump.
#define RTC_LOG_MAGIC 0xDEADBEEFu

/// RTC ring buffer structure for storing last 5 error log entries.
/// Placed in RTC NOINIT memory so it survives watchdog resets.
typedef struct {
    uint32_t magic;                                              ///< Magic number to validate buffer integrity
    uint8_t  head;                                               ///< Index of oldest entry
    uint8_t  count;                                              ///< Number of valid entries (0..BMS_LOG_ENTRY_COUNT)
    char     entries[BMS_LOG_ENTRY_COUNT][BMS_LOG_ENTRY_MAXLEN]; ///< Ring buffer of formatted error strings
} rtc_log_buf_t;

/// RTC NOINIT ring buffer instance (survives soft resets, cleared on power-on)
static RTC_NOINIT_ATTR rtc_log_buf_t s_rtc_log;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes the logging system with default log levels for all modules. Lower log levels than
/// selected will be suppressed.
///
/// \param None
/// \return None
void bms_logging_init(void)
{
    // Set default global log level to WARNING
    esp_log_level_set("*", ESP_LOG_WARN);

    // Validate RTC log buffer. If magic is invalid clear it.
    if (s_rtc_log.magic != RTC_LOG_MAGIC) {
        memset(&s_rtc_log, 0, sizeof(s_rtc_log));
        s_rtc_log.magic = RTC_LOG_MAGIC;
    }

    return;
}

/// This function sets the global log level for all modules. Lower log levels than selected will be suppressed.
///
/// \param[in] level Log level to set
/// \return None
void bms_logging_set_global_level(esp_log_level_t level)
{
    esp_log_level_set("*", level);

    return;
}

/// This function sets the log level for a specific module identified by its tag. It overrides the global log setting
///
/// \param[in] module_tag Module tag string
/// \param[in] level Log level to set
/// \return None
void bms_logging_set_module_level(const char *module_tag, esp_log_level_t level)
{
    if (module_tag != NULL) {
        esp_log_level_set(module_tag, level);
    }

    return;
}

/// This function stores a formatted error log message into the RTC NOINIT ring buffer.
/// Oldest entry is overwritten when buffer is full.
///
/// \param[in] tag Module tag string
/// \param[in] fmt printf-style format string
/// \param[in] ... Format arguments
/// \return None
void bms_log_rtc_store(const char *tag, const char *fmt, ...)
{
    // Write into next slot (overwrite oldest if full)
    uint8_t idx = (s_rtc_log.head + s_rtc_log.count) % BMS_LOG_ENTRY_COUNT;
    // If buffer is full, advance head to overwrite oldest entry
    if (s_rtc_log.count >= BMS_LOG_ENTRY_COUNT) {
        s_rtc_log.head = (s_rtc_log.head + 1) % BMS_LOG_ENTRY_COUNT;
    } else {
        s_rtc_log.count++;
    }

    // Format [TAG] message
    int off = snprintf(s_rtc_log.entries[idx], BMS_LOG_ENTRY_MAXLEN, "[%s] ", tag ? tag : "?");
    if (off < 0) off = 0;
    if ((size_t)off < BMS_LOG_ENTRY_MAXLEN) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(s_rtc_log.entries[idx] + off, BMS_LOG_ENTRY_MAXLEN - (size_t)off, fmt, args);
        va_end(args);
    }

    return;
}

/// This function retrieves all error log entries from the RTC ring buffer in chronological order (oldest first).
///
/// \param[out] out Output array of strings (must be at least ::BMS_LOG_ENTRY_COUNT elements of ::BMS_LOG_ENTRY_MAXLEN)
/// \param[out] count Number of valid entries copied
/// \return None
void bms_log_rtc_get_entries(char out[][BMS_LOG_ENTRY_MAXLEN], int *count)
{
    if (!out || !count) return;

    if (s_rtc_log.magic != RTC_LOG_MAGIC || s_rtc_log.count == 0) {
        *count = 0;
        return;
    }

    uint8_t n = s_rtc_log.count;
    if (n > BMS_LOG_ENTRY_COUNT) n = BMS_LOG_ENTRY_COUNT;

    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (s_rtc_log.head + i) % BMS_LOG_ENTRY_COUNT;
        memcpy(out[i], s_rtc_log.entries[idx], BMS_LOG_ENTRY_MAXLEN);
        out[i][BMS_LOG_ENTRY_MAXLEN - 1] = '\0';
    }
    *count = n;

    return;
}

/// This function clears the RTC error log ring buffer.
///
/// \param None
/// \return None
void bms_log_rtc_clear(void)
{
    memset(&s_rtc_log, 0, sizeof(s_rtc_log));
    s_rtc_log.magic = RTC_LOG_MAGIC;

    return;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/