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
/// Message counter for telemetry inclusion into the JSON.
static uint32_t s_message_counter = 0;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function serializes one stats window to JSON buffer.
///
/// \param[in] st Pointer to statistics structure to serialize
/// \param[out] buf Pointer to output buffer
/// \param[in] buf_size Size of output buffer in bytes
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
        "{\"device_id\":\"%s\",\"timestamp\":%u,\"sample_count\":%u,\"cell_errors\":%lu,",
        device_id,
        (unsigned)st->timestamp,
        (unsigned)st->sample_count,
        (unsigned long)st->cell_errors);
    
    // cell_v_avg array
    JSON_APPEND(off, buf, buf_size, "\"cell_v_avg\":[");
    for (int i = 0; i < nc; ++i) {
        JSON_APPEND(off, buf, buf_size, "%s%.3f", i ? "," : "", st->cell_v_avg[i]);
    }
    JSON_APPEND(off, buf, buf_size, "],");

    // Pack voltage average
    JSON_APPEND(off, buf, buf_size,
        "\"pack_v_avg\":%.3f",
        st->pack_v_avg);

    // Pack current average (only if current measurement is enabled)
    if (g_cfg.battery.current_enable) {
        JSON_APPEND(off, buf, buf_size,
            ",\"pack_i_avg\":%.3f",
            st->pack_i_avg);
    }

    // Temperature average (only if temperature measurement is enabled)
    if (g_cfg.battery.temperature_enable) {
        JSON_APPEND(off, buf, buf_size,
            ",\"temperature_avg\":%.2f",
            st->temperature_avg);
    }

    // Configuration limit values in separate config object
    JSON_APPEND(off, buf, buf_size,
        ",\"config\":{\"cell_v_min\":%.3f,\"cell_v_max\":%.3f",
        g_cfg.battery.cell_v_min, g_cfg.battery.cell_v_max);
    if (g_cfg.battery.current_enable) {
        JSON_APPEND(off, buf, buf_size,
            ",\"series_pack_i_min\":%.3f,\"series_pack_i_max\":%.3f",
            g_cfg.battery.series_pack_i_min, g_cfg.battery.series_pack_i_max);
    }
    JSON_APPEND(off, buf, buf_size, "}");

    // Add telemetry data if this is the 10th message
    if (include_telemetry) {
        esp32_telemetry_t esp_telem;
        ltc6804_status_t ltc_status;
        
        telemetry_get_esp32_telemetry(&esp_telem);
        telemetry_get_ltc6804_status(&ltc_status);
        
        JSON_APPEND(off, buf, buf_size,
            ",\"telemetry\":{\"sw_version\":\"%s\",\"cpu_load\":%u,"
            "\"free_heap\":%u,\"min_heap\":%u,\"reset_reason\":%u",
            sw_version,
            (unsigned)esp_telem.cpu_load,
            (unsigned)esp_telem.free_heap,
            (unsigned)esp_telem.min_free_heap,
            (unsigned)esp_telem.reset_reason);

        // Include last error messages if reset was caused by TWDT
        if (esp_telem.reset_msg[0] != '\0') {
            JSON_APPEND(off, buf, buf_size,
                ",\"reset_msg\":\"%s\"",
                esp_telem.reset_msg);
        }
        
        if (ltc_status.valid) {
            JSON_APPEND(off, buf, buf_size,
                ",\"ltc_soc\":%u,\"ltc_itmp\":%u,\"ltc_va\":%u"
                ",\"ltc_vd\":%u,\"ltc_cell_flags\":%lu,\"ltc_diag\":%u",
                (unsigned)ltc_status.soc,
                (unsigned)ltc_status.itmp,
                (unsigned)ltc_status.va,
                (unsigned)ltc_status.vd,
                (unsigned long)ltc_status.cell_flags,
                (unsigned)ltc_status.diag);
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
