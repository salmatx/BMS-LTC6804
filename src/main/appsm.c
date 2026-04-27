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
#define MAX_SAMPLES_PER_POP 100


/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/
/// Enumeration of application states
typedef enum
{
    APP_ST_UNDEFINED = 0u,      ///< Undefined state (used as initial value)
    APP_ST_INIT = 1u,           ///< Initialization state
    APP_ST_PROCESSING = 2u,     ///< Processing state
    APP_ST_CONFIG = 3u,         ///< Configuration state
} app_state_t;

/// Structure defining application state machine data.
typedef struct
{
    app_state_t prev_state;     ///< Previous application state
    app_state_t curr_state;     ///< Current application state
    app_state_t next_state;     ///< Next application state
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
/// This function executes one iteration of the application state machine. State machine execution includes:
/// 1. Input handling of current state
/// 2. State-specific processing
/// 3. Output of current state handling
/// Note that functions within state processing return the next state to transition to.
///
/// \param None
/// \return None
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
/// This function checks if the configuration mode flag is set in NVS. If set, the flag is cleared
/// to prevent re-entering configuration mode on next boot.
///
/// \param None
/// \return True if config mode flag was set, false otherwise
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

/// Execute one state-machine cycle while the application is in CONFIG mode.
/// Keep data acquisition/statistics history alive for the web UI while configuration
/// pages are open, but avoid MQTT publishing and normal processing-state traffic.
///
/// State machine execution includes:
/// 1. Pops pending samples from the inter-core queue into the local ring buffer.
/// 2. Computes statistics windows from buffered samples.
/// 3. Serializes computed stats to JSON and pushes them only to HTTP history storage.
/// 4. Sleeps for 1 second to reduce CPU usage during CONFIG mode.
///
/// The function always returns ::APP_ST_CONFIG, so the state machine remains in
/// CONFIG until another part of the system requests a reboot/transition.
///
/// \param None
/// \return Next application state (always ::APP_ST_CONFIG)
static app_state_t state_config_handler(void)
{
    app_state_t ret_state = APP_ST_CONFIG;

    // Pop samples from inter-core queue into ring buffer
    while (buf.count < buf.capacity) {
        bms_sample_t sample;
        if (!bms_queue_pop(&sample)) {
            break;
        }
        size_t idx = bms_buf_index(&buf, buf.count);
        buf.samples[idx] = sample;
        buf.count++;
    }

    // Compute stats and push to HTTP history
    bms_stats_buffer_t stats_buf;
    char json_buf[BMS_STATS_JSON_MAXLEN];

    while (buf.count > 0) {
        size_t used_samples = bms_compute_stats(&buf, &stats_buf);
        if (used_samples == 0) {
            break;
        }

        for (size_t i = 0; i < stats_buf.stats_count; ++i) {
            const bms_stats_t *st = &stats_buf.stats_array[i];
            int len = bms_stats_to_json(st, json_buf, sizeof(json_buf));
            if (len < 0) {
                BMS_LOGE("Failed to serialize stats to JSON");
                break;
            }
            bms_stats_hist_push(json_buf, (size_t)len);
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    return ret_state;
}

/// This function handles the INIT application state. Function executes initialization logic
/// and transitions to PROCESSING state if initialization is successful, or to CONFIG state
/// if initialization fails (e.g. invalid/missing configuration).
///
/// \param None
/// \return Next application state (::APP_ST_PROCESSING or ::APP_ST_CONFIG)
static app_state_t state_init_handler(void)
{
    app_state_t ret_state = APP_ST_INIT;
    
    if (initialization_exec()) {
        ret_state = APP_ST_PROCESSING;
    } else {
        BMS_LOGW("Initialization not completed, entering CONFIG state");
        ret_state = APP_ST_CONFIG;
    }

    return ret_state;
}

/// This function handles the PROCESSING application state. Function performs the following:
/// 1. Pops samples from inter-core queue into ring buffer
/// 2. Computes statistics windows from samples in ring buffer
/// 3. Publishes computed statistics via MQTT using QoS 0
/// 4. Stores published statistics in local history buffer for web interface
/// Note that function remains in PROCESSING state unless config mode flag is set.
///
/// \param None
/// \return Next application state (always ::APP_ST_PROCESSING or ::APP_ST_CONFIG if config mode flag set)
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
    char json_buf[BMS_STATS_JSON_MAXLEN];

    // 2.1) Process all available samples in ring buffer 
    while (buf.count > 0) {
        size_t used_samples = bms_compute_stats(&buf, &stats_buf);
        if (used_samples == 0) {
            break; // not enough samples to compute stats
        }

        // 2.2) Publish computed stats via MQTT in JSON format
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

            // Store latest sample for HTTP stats endpoint (browser caches history)
            bms_stats_hist_push(json_buf, (size_t)len);

            // Log published stats
            BMS_LOGI("STAT[%u]: ts=%lu ticks, samples=%u, cell_errors=0x%08lX",
                        (unsigned)i,
                        (unsigned long)st->timestamp,
                        (unsigned)st->sample_count,
                        (unsigned long)st->cell_errors);
        }
    }

    return ret_state;
}

/// This function handles input processing for all application states. Function checks if current state
/// has changed from previous state, and if so, executes state-specific input handling logic.
///
/// \param None
/// \return None
static void state_input_handler(void)
{
    if (s_appsm.prev_state != s_appsm.curr_state) {
        switch (s_appsm.curr_state)
        {
            case APP_ST_INIT: {
                // 1) Mount SPIFFS
                esp_err_t err = bms_spiffs_init();
                if (err != ESP_OK) {
                    BMS_LOGE("SPIFFS init failed: %s", esp_err_to_name(err));
                    break;
                }

                // 2) Load config overrides (keeps defaults if file missing/bad)
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
                break;

            case APP_ST_CONFIG:
                BMS_LOGI("Entering CONFIG state");
                if (s_appsm.prev_state == APP_ST_PROCESSING) {
                    BMS_LOGI("Tasks and watchdogs remain active during CONFIG state");
                } else {
                    BMS_LOGI("Entering CONFIG state from INIT (AP mode) - HTTP server active for configuration");
                }
                break;

            default:
                break;
        }
    }
    return;
}

/// This function handles output processing for all application states. Function checks if the next state
/// is different from the current state, and if so, executes state-specific output handling logic.
///
/// \param None
/// \return None
static void state_output_handler(void)
{
    if (s_appsm.next_state != s_appsm.curr_state) {
        switch (s_appsm.curr_state)
        {
            case APP_ST_INIT:
                // Slow core TWDT is created separately after finishing initialization
                // to avoid triggering it during long initialization sequences.
                slow_core_TWDT_create();
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

            case APP_ST_PROCESSING:
                // Free allocated buffer
                free(buf.samples);
                buf.samples = NULL;
                break;

            case APP_ST_CONFIG:
                free(buf.samples);
                buf.samples = NULL;
                break;

            default:
                break;
        }
    }
    return;
}
