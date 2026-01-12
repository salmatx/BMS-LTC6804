#pragma once
#include <stddef.h>
#include <stdint.h>

#define BMS_HTTP_SECONDS           60u
#define BMS_MAX_WINDOWS_PER_SEC    4u
#define BMS_HTTP_HISTORY_CAPACITY  (BMS_HTTP_SECONDS * BMS_MAX_WINDOWS_PER_SEC)

#define BMS_STATS_JSON_MAXLEN      512u

struct httpd_req;
void bms_stats_hist_push(const char *json, size_t len);
int  bms_stats_hist_send_as_json_array(struct httpd_req *req);
