/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "appsm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "bms_data.h"
#include "intercore_comm.h"
#include "logging.h"
#include "process.h"
#include "mqtt.h"
#include "json_formatter.h"
#include "initialization.h"
#include "tasksSC.h"
#include "stats_history.h"

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
static app_state_t state_config_handler(void)
{
    app_state_t ret_state = APP_ST_CONFIG;
    // Placeholder for state config handling logic
    return ret_state;
}

static app_state_t state_init_handler(void)
{
    app_state_t ret_state = APP_ST_INIT;
    
    if (initialization_exec()) {
        ret_state = APP_ST_PROCESSING;
    }

    return ret_state;
}

static app_state_t state_processing_handler(void)
{
    app_state_t ret_state = APP_ST_PROCESSING;

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

    // 2) Compute stats and publish them. Only consume samples after publish ACK.
    bms_stats_buffer_t stats_buf;
    // Buffer for JSON serialization
    char json_buf[512];

    // 2.1) Process all available samples in ring buffer 
    while (buf.count > 0) {
        // TODO: Consider computing stats until ring buffer is empty in one call instead of looping. Or at least
        //       compute stats until ring buffer is empty (using multiple calls) and send data via MQTT only once in buffer.
        //       Sending multiple MQTT messages in one Slow Core cycle may lead to congestion and delays.

        // Compute next stats window without consuming samples
        int used_samples = bms_compute_stats(&buf, &stats_buf);
        if (used_samples <= 0) {
            break; // not enough samples to compute stats
        }

        bool all_sent = true;

        // 2.2) Publish computed stats via MQTT in JSON format.
        for (size_t i = 0; i < stats_buf.stats_count; ++i) {
            const bms_stats_t *st = &stats_buf.stats_array[i];

            // Serialize stats to JSON
            int len = bms_stats_to_json(st, json_buf, sizeof(json_buf));
            if (len < 0) {
                BMS_LOGE("Failed to serialize stats to JSON");
                all_sent = false;
                break;
            }

            // Publish and wait until broker ACKs the message (QoS1 => MQTT_EVENT_PUBLISHED).
            esp_err_t perr = bms_mqtt_publish_blocking_qos1(
                "bms/esp32/stats",
                json_buf,
                len,
                pdMS_TO_TICKS(MQTT_PUBACK_TIMEOUT_MS)
            );

            if (perr != ESP_OK) {
                BMS_LOGW("MQTT publish not acknowledged (%s). Not consuming samples; retry next cycle.",
                            esp_err_to_name(perr));
                all_sent = false;
                break;
            }

            bms_stats_hist_push(json_buf, (size_t)len);

            // Log only after successful ACK
            BMS_LOGI("STAT[%u]: ts=%lu ticks, samples=%u, cell_errors=0x%04X",
                        (unsigned)i,
                        (unsigned long)st->timestamp,
                        (unsigned)st->sample_count,
                        (unsigned)st->cell_errors);
        }

        if (all_sent) {
            // Consume raw samples only after all windows for this 1s chunk were sent successfully
            remove_processed_samples(&buf, used_samples);
        } else {
            // Prevent computing/sending next stats until current is sent successfully
            break;
        }
    }

    return ret_state;
}

static void state_input_handler(void)
{
    if (s_appsm.prev_state != s_appsm.curr_state) {
        switch (s_appsm.curr_state)
        {
            case APP_ST_INIT:
                // Placeholder for configuration file loading.
                break;

            case APP_ST_PROCESSING:
                // Initialize ring buffer used to stage samples popped from inter-core queue
                buf.capacity = MAX_SAMPLES_PER_POP;
                buf.head     = 0;
                buf.count    = 0;
                buf.samples  = malloc(sizeof(bms_sample_t) * buf.capacity);     //TODO: May want to resize stack and remove dynamic
                                                                                // allocation if no other locations use dynamic memory
                // Check allocation and delete task on failure
                if (!buf.samples) {
                    BMS_LOGE("Failed to allocate samples buffer");
                    vTaskDelete(NULL);
                }
                break;

            case APP_ST_CONFIG:
                // Placeholder for state input handling logic for CONFIG state
                break;

            default:
                break;
        }
    }
    return;
}

static void state_output_handler(void)
{
    if (s_appsm.prev_state != s_appsm.curr_state) {
        switch (s_appsm.curr_state)
        {
            case APP_ST_INIT:
                slow_core_TWDT_create();
                break;

            case APP_ST_PROCESSING:
                // Placeholder for state output handling logic for PROCESSING state
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