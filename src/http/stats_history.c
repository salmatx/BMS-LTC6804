#include "stats_history.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_http_server.h"

typedef struct {
    char     json[BMS_STATS_JSON_MAXLEN];
    uint16_t len;
} hist_item_t;

static hist_item_t s_hist[BMS_HTTP_HISTORY_CAPACITY];
static uint16_t    s_head  = 0;  // next write position
static uint16_t    s_count = 0;  // valid entries: 0..CAPACITY
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void bms_stats_hist_push(const char *json, size_t len)
{
    if (!json || len == 0) return;

    taskENTER_CRITICAL(&s_lock);

    if (len >= BMS_STATS_JSON_MAXLEN) len = BMS_STATS_JSON_MAXLEN - 1;

    memcpy(s_hist[s_head].json, json, len);
    s_hist[s_head].json[len] = '\0';
    s_hist[s_head].len = (uint16_t)len;

    s_head = (uint16_t)((s_head + 1u) % BMS_HTTP_HISTORY_CAPACITY);
    if (s_count < BMS_HTTP_HISTORY_CAPACITY) s_count++;

    taskEXIT_CRITICAL(&s_lock);
}

int bms_stats_hist_send_as_json_array(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (httpd_resp_send_chunk(req, "[", 1) != ESP_OK) return ESP_FAIL;

    uint16_t head, count;
    taskENTER_CRITICAL(&s_lock);
    head  = s_head;
    count = s_count;
    taskEXIT_CRITICAL(&s_lock);

    uint16_t start = (uint16_t)((head + BMS_HTTP_HISTORY_CAPACITY - count) % BMS_HTTP_HISTORY_CAPACITY);

    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (uint16_t)((start + i) % BMS_HTTP_HISTORY_CAPACITY);

        if (i != 0) {
            if (httpd_resp_send_chunk(req, ",", 1) != ESP_OK) {
                httpd_resp_send_chunk(req, NULL, 0);
                return ESP_FAIL;
            }
        }

        char tmp[BMS_STATS_JSON_MAXLEN];
        uint16_t tmplen;

        taskENTER_CRITICAL(&s_lock);
        tmplen = s_hist[idx].len;
        if (tmplen >= BMS_STATS_JSON_MAXLEN) tmplen = BMS_STATS_JSON_MAXLEN - 1;
        if (tmplen > 0) memcpy(tmp, s_hist[idx].json, tmplen);
        tmp[tmplen] = '\0';
        taskEXIT_CRITICAL(&s_lock);

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
