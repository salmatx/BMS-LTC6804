/// This module provides functions to format BMS statistics into JSON strings. JSON is used for
/// transmitting statistics via MQTT.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"
#include "json_formatter.h"
#include <stdio.h>

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_JSON"

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

    // Format statistics into JSON string and write into buffer
    int len = snprintf(buf, buf_size,
        "{"
          "\"timestamp\":%u,"
          "\"sample_count\":%u,"
          "\"cell_errors\":%u,"
          "\"cell_v_avg\":[%.3f,%.3f,%.3f,%.3f,%.3f],"
          "\"cell_v_min\":[%.3f,%.3f,%.3f,%.3f,%.3f],"
          "\"cell_v_max\":[%.3f,%.3f,%.3f,%.3f,%.3f],"
          "\"pack_v_avg\":%.3f,"
          "\"pack_v_min\":%.3f,"
          "\"pack_v_max\":%.3f,"
          "\"pack_i_avg\":%.3f,"
          "\"pack_i_min\":%.3f,"
          "\"pack_i_max\":%.3f"
        "}",
        (unsigned)st->timestamp,
        (unsigned)st->sample_count,
        (unsigned)st->cell_errors,
        st->cell_v_avg[0], st->cell_v_avg[1], st->cell_v_avg[2],
        st->cell_v_avg[3], st->cell_v_avg[4],
        st->cell_v_min[0], st->cell_v_min[1], st->cell_v_min[2],
        st->cell_v_min[3], st->cell_v_min[4],
        st->cell_v_max[0], st->cell_v_max[1], st->cell_v_max[2],
        st->cell_v_max[3], st->cell_v_max[4],
        st->pack_v_avg,
        st->pack_v_min,
        st->pack_v_max,
        st->pack_i_avg,
        st->pack_i_min,
        st->pack_i_max
    );

    if (len < 0 || (size_t)len >= buf_size) {
        BMS_LOGE("JSON serialization truncated or failed");
        return -1;
    }
    return len;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
