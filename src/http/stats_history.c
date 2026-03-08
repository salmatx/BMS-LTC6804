/// This module stores the latest JSON-formatted BMS statistics sample
/// and provides functionality to send it as a single JSON object via HTTP response.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "stats_history.h"
#include "logging.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_http_server.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_HIST"

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
/// Latest statistics sample JSON string
static char s_latest_json[BMS_STATS_JSON_MAXLEN];
/// Length of the latest JSON string
static uint16_t s_latest_len = 0;
/// Flag indicating if a sample has been stored
static bool s_has_sample = false;
/// Synchronization lock for thread safety
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes the latest statistics sample storage.
/// Must be called once before any push or send operations.
///
/// \param None
/// \return ESP_OK always
esp_err_t bms_stats_hist_init(void)
{
    s_latest_len = 0;
    s_has_sample = false;
    BMS_LOGI("Stats latest-sample store initialized");
    return ESP_OK;
}

/// This function stores the latest JSON-formatted statistics sample,
/// overwriting any previously stored sample.
///
/// \param json Pointer to JSON string
/// \param len Length of JSON string
/// \return None
void bms_stats_hist_push(const char *json, size_t len)
{
    if (!json || len == 0) return;

    taskENTER_CRITICAL(&s_lock);

    if (len >= BMS_STATS_JSON_MAXLEN) len = BMS_STATS_JSON_MAXLEN - 1;

    memcpy(s_latest_json, json, len);
    s_latest_json[len] = '\0';
    s_latest_len = (uint16_t)len;
    s_has_sample = true;

    taskEXIT_CRITICAL(&s_lock);
}

/// This function sends the latest statistics sample as a single JSON object via HTTP response.
/// Returns JSON "null" if no sample is available yet.
///
/// \param req Pointer to HTTP request structure
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_stats_hist_send_latest(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char tmp[BMS_STATS_JSON_MAXLEN];
    uint16_t tmplen;
    bool has;

    taskENTER_CRITICAL(&s_lock);
    has    = s_has_sample;
    tmplen = s_latest_len;
    if (has && tmplen > 0) {
        if (tmplen >= BMS_STATS_JSON_MAXLEN) tmplen = BMS_STATS_JSON_MAXLEN - 1;
        memcpy(tmp, s_latest_json, tmplen);
    }
    tmp[tmplen] = '\0';
    taskEXIT_CRITICAL(&s_lock);

    if (!has || tmplen == 0) {
        return httpd_resp_sendstr(req, "null");
    }

    return httpd_resp_send(req, tmp, tmplen);
}
