/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "appsm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "bms_data.h"
#include "intercore_comm.h"
#include "logging.h"
#include "process.h"
#include "mqtt.h"
#include "json_formatter.h"
#include "initialization.h"
#include "tasksSC.h"
#include "tasksFC.h"
#include "stats_history.h"
#include "spiffs.h"
#include "configuration.h"
#include "watchdog.h"


/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "APP_STATES"


/// Maximum samples to pop from FreeRTOS queue in one Slow Core processing cycle.
#define MAX_SAMPLES_PER_POP    100


/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/
typedef enum
{
    APP_ST_UNDEFINED = 0u,
    APP_ST_INIT = 1u,
    APP_ST_PROCESSING = 2u,
    APP_ST_CONFIG = 3u,
} app_state_t;


typedef struct
{
    app_state_t prev_state;
    app_state_t curr_state;
    app_state_t next_state;
} appsm_t;


/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static void state_input_handler(void);
static void state_output_handler(void);
static app_state_t state_config_handler(void);
static app_state_t state_init_handler(void);
static app_state_t state_processing_handler(void);
static bool check_config_mode_flag(void);


/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/


/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
static appsm_t s_appsm = {
    .prev_state = APP_ST_UNDEFINED,
    .curr_state = APP_ST_INIT,
    .next_state = APP_ST_INIT,
};


/// Ring buffer used to stage samples popped from inter-core queue
static bms_sample_buffer_t buf;


/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/



/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
void app_states_exec(void)
{
    state_input_handler();


    switch (s_appsm.curr_state)
    {
        case APP_ST_INIT:
            s_appsm.next_state = state_init_handler();
            break;


        case APP_ST_PROCESSING:
            s_appsm.next_state = state_processing_handler();
            break;


        case APP_ST_CONFIG:
            s_appsm.next_state = state_config_handler();
            break;


        default:
            break;
    }


    state_output_handler();


    s_appsm.prev_state = s_appsm.curr_state;
    s_appsm.curr_state = s_appsm.next_state;


    return;
}


/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
static bool check_config_mode_flag(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }


    uint8_t config_mode = 0;
    err = nvs_get_u8(nvs_handle, "config_mode", &config_mode);
    
    if (err == ESP_OK && config_mode == 1) {
        // Clear flag immediately to prevent entering CONFIG on next boot
        BMS_LOGI("Config mode flag detected, clearing it");
        nvs_set_u8(nvs_handle, "config_mode", 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        return true;
    }
    
    nvs_close(nvs_handle);
    return false;
}




static app_state_t state_config_handler(void)
{
    app_state_t ret_state = APP_ST_CONFIG;


    vTaskDelay(pdMS_TO_TICKS(1000));
    return ret_state;
}


static app_state_t state_init_handler(void)
{
    app_state_t ret_state = APP_ST_INIT;
    
    if (initialization_exec()) {
        ret_state = APP_ST_PROCESSING;
    } else {
        BMS_LOGW("Invalid/missing config, entering CONFIG state");
        ret_state = APP_ST_CONFIG;
    }


    return ret_state;
}


static app_state_t state_processing_handler(void)
{
    app_state_t ret_state = APP_ST_PROCESSING;


    if (check_config_mode_flag()) {
        BMS_LOGI("Config mode flag set in NVS, entering CONFIG state");
        ret_state = APP_ST_CONFIG;
        return ret_state;
    }


    // 1) Pop samples from inter-core queue into ring buffer. Fill up to capacity.
    while (buf.count < buf.capacity) {
        bms_sample_t sample;
        if (!bms_queue_pop(&sample)) {
            break; // queue empty
        }


        size_t idx = bms_buf_index(&buf, buf.count);
        buf.samples[idx] = sample;
        buf.count++;
    }


    // 2) Compute stats and publish them using QoS 0 (fire-and-forget)
    bms_stats_buffer_t stats_buf;
    // Buffer for JSON serialization
    char json_buf[512];


    // 2.1) Process all available samples in ring buffer 
    while (buf.count > 0) {
        // Compute next stats window without consuming samples
        int used_samples = bms_compute_stats(&buf, &stats_buf);
        if (used_samples <= 0) {
            break; // not enough samples to compute stats
        }


        // 2.2) Publish computed stats via MQTT in JSON format using QoS 0
        for (size_t i = 0; i < stats_buf.stats_count; ++i) {
            const bms_stats_t *st = &stats_buf.stats_array[i];


            // Serialize stats to JSON
            int len = bms_stats_to_json(st, json_buf, sizeof(json_buf));
            if (len < 0) {
                BMS_LOGE("Failed to serialize stats to JSON");
                break;
            }


            // Publish with QoS 0 - fire and forget, no acknowledgment needed
            esp_err_t perr = bms_mqtt_publish_qos0(
                "bms/esp32/stats",
                json_buf,
                len
            );


            if (perr != ESP_OK) {
                BMS_LOGW("MQTT publish failed (%s). Message dropped.", esp_err_to_name(perr));
                // Continue processing - don't block on publish failures with QoS 0
            }


            // Store in history regardless of MQTT success (for local web interface)
            bms_stats_hist_push(json_buf, (size_t)len);


            // Log published stats
            BMS_LOGI("STAT[%u]: ts=%lu ticks, samples=%u, cell_errors=0x%04X",
                        (unsigned)i,
                        (unsigned long)st->timestamp,
                        (unsigned)st->sample_count,
                        (unsigned)st->cell_errors);
        }


        // Consume raw samples immediately after processing (no waiting for ACK)
        remove_processed_samples(&buf, used_samples);
    }


    return ret_state;
}


static void state_input_handler(void)
{
    if (s_appsm.prev_state != s_appsm.curr_state) {
        switch (s_appsm.curr_state)
        {
            case APP_ST_INIT: {
                /* 1) Mount SPIFFS so /spiffs/ paths exist */
                esp_err_t err = bms_spiffs_init();
                if (err != ESP_OK) {
                    BMS_LOGE("SPIFFS init failed: %s", esp_err_to_name(err));
                    break;
                }


                /* 2) Load config overrides (keeps defaults if file missing/bad) */
                err = configuration_load("/spiffs/config.json");
                if (err != ESP_OK) {
                    BMS_LOGW("Config not loaded (%s). Using defaults.", esp_err_to_name(err));
                } else {
                    BMS_LOGI("Config loaded: wifi_ssid=%s mqtt_uri=%s",
                             g_cfg.wifi.ssid, g_cfg.mqtt.uri);
                    BMS_LOGI("Battery cfg: cell_v_min=%0.3f cell_v_max=%0.3f",
                             (double)g_cfg.battery.cell_v_min, (double)g_cfg.battery.cell_v_max);
                }
                break;
            }


            case APP_ST_PROCESSING:
                // Initialize ring buffer used to stage samples popped from inter-core queue
                buf.capacity = MAX_SAMPLES_PER_POP;
                buf.head     = 0;
                buf.count    = 0;
                buf.samples  = malloc(sizeof(bms_sample_t) * buf.capacity);
                
                if (!buf.samples) {
                    BMS_LOGE("Failed to allocate samples buffer");
                    vTaskDelete(NULL);
                }
                break;


            case APP_ST_CONFIG:
                BMS_LOGI("Entering CONFIG state - cleaning up tasks and disabling watchdogs");
                
                // 1. Delete all Fast Core tasks (Core 1)
                fast_core_tasks_delete();
                
                // 2. Delete Slow Core feeder task
                slow_core_TWDT_delete();
                
                // 3. Give tasks time to clean up
                vTaskDelay(pdMS_TO_TICKS(100));
                
                // 4. Deinitialize TWDT (all tasks unregistered by now)
                bms_wdt_deinit();
                break;


            default:
                break;
        }
    }
    return;
}



static void state_output_handler(void)
{
    if (s_appsm.next_state != s_appsm.curr_state) {
        switch (s_appsm.curr_state)
        {
            case APP_ST_INIT:
                slow_core_TWDT_create();
                break;


            case APP_ST_PROCESSING:
                // Free allocated buffer
                free(buf.samples);
                buf.samples = NULL;
                break;


            case APP_ST_CONFIG:
                // Placeholder for configuration saving logic.
                break;


            default:
                break;
        }
    }
    return;
}
