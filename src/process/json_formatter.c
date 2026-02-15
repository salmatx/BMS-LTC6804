/// This module provides functions to format BMS statistics into JSON strings. JSON is used for
/// transmitting statistics via MQTT.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"
#include "json_formatter.h"
#include "configuration.h"
#include <stdio.h>

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_JSON"

/// Helper macro for JSON buffer append. Checks snprintf return value and remaining buffer space.
/// If truncation or error occurs, logs error and returns -1.
#define JSON_APPEND(off, buf, buf_size, ...)                                    \
    do {                                                                        \
        int _ret = snprintf((buf) + (off), (buf_size) - (off), __VA_ARGS__);    \
        if (_ret < 0 || (size_t)((off) + _ret) >= (buf_size)) {                 \
            BMS_LOGE("JSON serialization truncated or failed");                 \
            return -1;                                                          \
        }                                                                       \
        (off) += _ret;                                                          \
    } while (0)

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

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function serializes one stats window to JSON buffer.
///
/// \param st Pointer to statistics structure to serialize
/// \param buf Pointer to output buffer
/// \param buf_size Size of output buffer in bytes
/// \return Length of serialized JSON string on success, -1 on error
int bms_stats_to_json(const bms_stats_t *st, char *buf, size_t buf_size)
{
    if (!st || !buf || buf_size == 0) {
        return -1;
    }

    const int nc = g_cfg.battery.num_cells;
    int off = 0;

    // Opening brace + fixed scalar fields
    JSON_APPEND(off, buf, buf_size,
        "{\"timestamp\":%u,\"sample_count\":%u,\"cell_errors\":%u,",
        (unsigned)st->timestamp,
        (unsigned)st->sample_count,
        (unsigned)st->cell_errors);

    // cell_v_avg array
    JSON_APPEND(off, buf, buf_size, "\"cell_v_avg\":[");
    for (int i = 0; i < nc; ++i) {
        JSON_APPEND(off, buf, buf_size, "%s%.3f", i ? "," : "", st->cell_v_avg[i]);
    }
    JSON_APPEND(off, buf, buf_size, "],");

    // cell_v_min array
    JSON_APPEND(off, buf, buf_size, "\"cell_v_min\":[");
    for (int i = 0; i < nc; ++i) {
        JSON_APPEND(off, buf, buf_size, "%s%.3f", i ? "," : "", st->cell_v_min[i]);
    }
    JSON_APPEND(off, buf, buf_size, "],");

    // cell_v_max array
    JSON_APPEND(off, buf, buf_size, "\"cell_v_max\":[");
    for (int i = 0; i < nc; ++i) {
        JSON_APPEND(off, buf, buf_size, "%s%.3f", i ? "," : "", st->cell_v_max[i]);
    }
    JSON_APPEND(off, buf, buf_size, "],");

    // Pack voltage fields
    JSON_APPEND(off, buf, buf_size,
        "\"pack_v_avg\":%.3f,\"pack_v_min\":%.3f,\"pack_v_max\":%.3f",
        st->pack_v_avg, st->pack_v_min, st->pack_v_max);

    // Pack current fields (only if current measurement is enabled)
    if (g_cfg.battery.current_enable) {
        JSON_APPEND(off, buf, buf_size,
            ",\"pack_i_avg\":%.3f,\"pack_i_min\":%.3f,\"pack_i_max\":%.3f",
            st->pack_i_avg, st->pack_i_min, st->pack_i_max);
    }

    // Closing brace
    JSON_APPEND(off, buf, buf_size, "}");

    return off;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
