/// This module implements a history buffer for storing JSON-formatted BMS statistics windows
/// and provides functionality to send the stored data as a JSON array via HTTP response.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "stats_history.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_http_server.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Definition of time span for statistics history to be saved and displayed via HTTP (in seconds).
#define BMS_HTTP_SECONDS           60u
/// Maximum count of statistics windows stored per second.
#define BMS_MAX_WINDOWS_PER_SEC    4u
/// Total capacity of statistics history buffer.
#define BMS_HTTP_HISTORY_CAPACITY  (BMS_HTTP_SECONDS * BMS_MAX_WINDOWS_PER_SEC)

/// Maximum length of JSON string for one statistics window.
#define BMS_STATS_JSON_MAXLEN      512u

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/
/// Structure representing one history item
typedef struct {
    char     json[BMS_STATS_JSON_MAXLEN];   ///< JSON string of the statistics window
    uint16_t len;                           ///< Length of the JSON string
} hist_item_t;

/// Structure representing the circular buffer for statistics history
typedef struct {
    hist_item_t items[BMS_HTTP_HISTORY_CAPACITY];  ///< Circular buffer array
    uint16_t    head;                              ///< Next write position
    uint16_t    count;                             ///< Valid entries count
    uint16_t    capacity;                          ///< Buffer capacity
    portMUX_TYPE lock;                             ///< Synchronization lock for thread safety
} stats_history_buffer_t;

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Circular buffer storing historical statistics windows
static stats_history_buffer_t s_history_buffer = {
    .head = 0,
    .count = 0,
    .capacity = BMS_HTTP_HISTORY_CAPACITY,
    .lock = portMUX_INITIALIZER_UNLOCKED
};

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function pushes a new JSON-formatted statistics window into the history buffer.
/// Note that if the buffer is full, the oldest entry is overwritten.
///
/// \param json Pointer to JSON string
/// \param len Length of JSON string
/// \return None
void bms_stats_hist_push(const char *json, size_t len)
{
    if (!json || len == 0) return;

    taskENTER_CRITICAL(&s_history_buffer.lock);

    if (len >= BMS_STATS_JSON_MAXLEN) len = BMS_STATS_JSON_MAXLEN - 1;

    memcpy(s_history_buffer.items[s_history_buffer.head].json, json, len);
    s_history_buffer.items[s_history_buffer.head].json[len] = '\0';
    s_history_buffer.items[s_history_buffer.head].len = (uint16_t)len;

    s_history_buffer.head = (uint16_t)((s_history_buffer.head + 1u) % s_history_buffer.capacity);
    if (s_history_buffer.count < s_history_buffer.capacity) s_history_buffer.count++;

    taskEXIT_CRITICAL(&s_history_buffer.lock);
}

/// This function sends the stored statistics history as a JSON array via HTTP response.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_stats_hist_send_as_json_array(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (httpd_resp_send_chunk(req, "[", 1) != ESP_OK) return ESP_FAIL;

    uint16_t head, count;
    taskENTER_CRITICAL(&s_history_buffer.lock);
    head  = s_history_buffer.head;
    count = s_history_buffer.count;
    taskEXIT_CRITICAL(&s_history_buffer.lock);

    uint16_t start = (uint16_t)((head + s_history_buffer.capacity - count) % s_history_buffer.capacity);

    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (uint16_t)((start + i) % s_history_buffer.capacity);

        if (i != 0) {
            if (httpd_resp_send_chunk(req, ",", 1) != ESP_OK) {
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }

        char tmp[BMS_STATS_JSON_MAXLEN];
        uint16_t tmplen;

        taskENTER_CRITICAL(&s_history_buffer.lock);
        tmplen = s_history_buffer.items[idx].len;
        if (tmplen >= BMS_STATS_JSON_MAXLEN) tmplen = BMS_STATS_JSON_MAXLEN - 1;
        if (tmplen > 0) memcpy(tmp, s_history_buffer.items[idx].json, tmplen);
        tmp[tmplen] = '\0';
        taskEXIT_CRITICAL(&s_history_buffer.lock);

        if (tmplen > 0) {
            if (httpd_resp_send_chunk(req, tmp, tmplen) != ESP_OK) {
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }
    }

    if (httpd_resp_send_chunk(req, "]", 1) != ESP_OK) {
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_FAIL;
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}
