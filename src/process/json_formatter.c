/// This module provides functions to format BMS statistics into JSON strings. JSON is used for
/// transmitting statistics via MQTT.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"
#include "json_formatter.h"
#include "configuration.h"
#include "telemetry.h"
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
/// Message counter for telemetry inclusion (every 10th message)
static uint32_t s_message_counter = 0;

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

    // Get basic device info
    char device_id[18];
    char sw_version[32];
    telemetry_get_device_id(device_id, sizeof(device_id));
    telemetry_get_sw_version(sw_version, sizeof(sw_version));
    
    // Check if we should include telemetry (every 10th message)
    bool include_telemetry = (s_message_counter % 10) == 0;
    s_message_counter++;

    const int nc = g_cfg.battery.num_cells;
    int off = 0;

    // Opening brace + device ID + timestamp
    JSON_APPEND(off, buf, buf_size,
        "{\"device_id\":\"%s\",\"timestamp\":%u,\"sample_count\":%u,\"cell_errors\":%u,",
        device_id,
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

    // Add telemetry data if this is the 10th message
    if (include_telemetry) {
        esp32_telemetry_t esp_telem;
        ltc6804_status_t ltc_status;
        
        telemetry_get_esp32_telemetry(&esp_telem);
        telemetry_get_ltc6804_status(&ltc_status);
        
        JSON_APPEND(off, buf, buf_size,
            ",\"telemetry\":{\"sw_version\":\"%s\",\"cpu_load\":%u,"
            "\"free_heap\":%u,\"min_heap\":%u",
            sw_version,
            (unsigned)esp_telem.cpu_load,
            (unsigned)esp_telem.free_heap,
            (unsigned)esp_telem.min_free_heap);
        
        if (ltc_status.valid) {
            JSON_APPEND(off, buf, buf_size,
                ",\"ltc_status_a\":%u,\"ltc_status_b\":%u",
                (unsigned)ltc_status.status_a,
                (unsigned)ltc_status.status_b);
        }
        
        JSON_APPEND(off, buf, buf_size, "}");
    }

    // Closing brace
    JSON_APPEND(off, buf, buf_size, "}");

    return off;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
