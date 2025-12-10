/// This module creates and manages application tasks running on both cores, including
/// real-time processing, communication, and watchdog feeding.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "app_tasks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "watchdog.h"
#include "bms_adapter.h"
#include "bms_data.h"
#include "intercore_comm.h"
#include "logging.h"
#include "process.h"
#include "mqtt.h"
#include "json_formatter.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "APP_TASKS"

/* Core 0 SW watchdog configuration (functional, not hard RT) */
/// Core 0 SW watchdog strobe period and timeout in miliseconds. Core 0 SW watchdog monitors duty cycle of Core 0 task.
/// If Core 0 task does not strobe within timeout period, HW TWDT is allowed to expire, causing system reset.
#define CORE0_SW_STROBE_MS    1000
/// Core 0 SW watchdog timeout in milliseconds.
#define CORE0_SW_TIMEOUT_MS  30000

/// TWDT feeding period in milliseconds. Udes by both Core 0 and Core 1 feeders.
#define WDT_FEED_MS             20

/// Core 1 real-time processing task period in milliseconds.
#define CORE1_PERIOD_MS         50

/// Maximum samples to pop from FreeRTOS queue in one Core 0 processing cycle.
#define MAX_SAMPLES_PER_POP    100

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static void core0_task(void *arg);
static void core1_task(void *arg);
static void core1_feeder_task(void *arg);
static void core0_feeder_task(void *arg);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Flag indicating whether feeding of HW TWDT is allowed. Set to false on Core 0 SW watchdog timeout.
static volatile bool s_allow_feeding = true;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function creates all application tasks on both cores. Taks handles are not used and thus not returned,
/// because it is not intended to manage tasks (suspend, delete etc,) after creation.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t app_tasks_create(void)
{
    BaseType_t result;

    // Create Core 0 task. Lower priority than Core 0 feeder to ensure feeder runs.
    result = xTaskCreatePinnedToCore(core0_task, "core0_task", 4096, NULL, 4, NULL, 0);
    if (result != pdPASS) {
        BMS_LOGE("Failed to create Core 0 task");
        return ESP_FAIL;
    }

    // Create Core 1 processing task. Higher priority than Core 1 feeder to ensure real-time processing.
    result = xTaskCreatePinnedToCore(core1_task, "core1_proc", 4096, NULL, 7, NULL, 1);
    if (result != pdPASS) {
        BMS_LOGE("Failed to create Core 1 processing task");
        return ESP_FAIL;
    }

    // Create Core 1 TWDT feeder task. Lower priority than Core 1 processing to ensure processing runs.
    result = xTaskCreatePinnedToCore(core1_feeder_task, "core1_feeder", 2048, NULL, 6, NULL, 1);
    if (result != pdPASS) {
        BMS_LOGE("Failed to create Core 1 feeder task");
        return ESP_FAIL;
    }

    // Create Core 0 TWDT feeder task. Higher priority than Core 0 task to ensure feeder runs.
    result = xTaskCreatePinnedToCore(core0_feeder_task, "core0_feeder", 2048, NULL, 5, NULL, 0);
    if (result != pdPASS) {
        BMS_LOGE("Failed to create Core 0 feeder task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// Core 0 task reads BMS samples from inter-core queue, computes statistics, and publishes them via MQTT.
/// This task also monitors its own duty cycle using a software watchdog mechanism. If the task fails to complete
/// its work within the defined timeout period, it disables feeding of the hardware TWDT, allowing it to expire
/// and reset the system.
///
/// \param arg Unused in current implementation and may be NULL
/// \return None
static void core0_task(void *arg)
{
    // TODO: remove parameters if unused in future implementation
    (void)arg;

    // SW watchdog timing parameters
    const TickType_t sw_check_ticks   = pdMS_TO_TICKS(CORE0_SW_STROBE_MS);
    const TickType_t sw_timeout_ticks = pdMS_TO_TICKS(CORE0_SW_TIMEOUT_MS);

    // Timing variables for SW watchdog
    TickType_t start;
    TickType_t end;

    // Initialize buffer for popping samples from FreeRTOS queue
    bms_sample_buffer_t buf;
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

    // Main Core 0 loop
    while (1)
    {
        // Start SW watchdog timing
        start = xTaskGetTickCount();

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

        // 2) Compute stats and publish then via MQTT until buffer is empty or not enough samples.
        // Output stats buffer
        bms_stats_buffer_t stats_buf;
        // Get MQTT client handle
        esp_mqtt_client_handle_t client = bms_mqtt_get_client();
        // Buffer for JSON serialization
        char json_buf[512];
        // 2.1) Process all available samples in ring buffer                                        
         while (buf.count > 0) {
            // TODO: Consider computing stats until ring buffer is empty in one call instead of looping. Or at least
            //       compute stats until ring buffer is empty (using multiple calls) and send data via MQTT only once in buffer.
            //       Sending multiple MQTT messages in one Core 0 cycle may lead to congestion and delays.
            
            // Compute stats for samples in ring buffer read from 1 s.
            if (!bms_compute_stats(&buf, &stats_buf)) {
                break;
            }

            // 2.2) Publish computed stats via MQTT in JSON format.
            for (size_t i = 0; i < stats_buf.stats_count; ++i) {
                const bms_stats_t *st = &stats_buf.stats_array[i];

                // Serialize stats to JSON
                int len = bms_stats_to_json(st, json_buf, sizeof(json_buf));
                if (len < 0) {
                    BMS_LOGE("Failed to serialize stats to JSON");
                    continue;
                }

                // Publish JSON via MQTT
                if (client) {
                    int msg_id = esp_mqtt_client_publish(
                        client,
                        "bms/esp32/stats",  // topic
                        json_buf,
                        len,
                        0,                  // QoS 0
                        0                   // no retain
                    );
                    if (msg_id < 0) {
                        BMS_LOGE("MQTT publish failed");
                    }
                } else {
                    BMS_LOGW("MQTT client not ready, dropping stats");
                }

                // Log published stats summary into stdout
                BMS_LOGI("STAT[%u]: ts=%lu ticks, samples=%u, cell_errors=0x%04X",
                         (unsigned)i,
                         (unsigned long)st->timestamp,
                         (unsigned)st->sample_count,
                         (unsigned)st->cell_errors);
            }
        }

        // End SW watchdog timing and check for timeout
        end = xTaskGetTickCount();
        // If Core 0 task overran SW watchdog timeout, disable TWDT feeding
        if ((end - start) > sw_timeout_ticks) {
            BMS_LOGE("Core 0 SW watchdog timeout (> %d ms), disabling HW WD feed", CORE0_SW_TIMEOUT_MS);
            s_allow_feeding = false;
        }

        // Puts task into blocked state for defined period
        vTaskDelay(sw_check_ticks);
    }
}


/// Core 1 task reads BMS samples from BMS adapter and pushes them into inter-core queue for Core 0 processing.
/// Core 1 task runs in real-time and must complete its work within defined period, otherwise it disables feeding of
/// the hardware TWDT, allowing it to expire and reset the system.
///
/// \param arg Unused in current implementation and may be NULL
/// \return None
static void core1_task(void *arg)
{
    // TODO: remove parameters if unused in future implementation
    (void)arg;

    const bms_adapter_t *bms = bms_get_adapter();
    if (!bms) {
        BMS_LOGE("No BMS adapter selected");
        vTaskDelete(NULL);
    }

    // Waiting period for real-time loop
    const TickType_t period = pdMS_TO_TICKS(CORE1_PERIOD_MS);
    // Variable handling previous wake time for accurate periodic delay
    TickType_t last_wake = xTaskGetTickCount();
    // Timing variables for checking real-time overrun
    TickType_t start;
    TickType_t end;

    bms_sample_t sample;

    // Main Core 1 loop
    while (1)
    {
        // Start timing for real-time overrun check
        start = xTaskGetTickCount();

        // Check free slots in inter-core queue. If none, disable feeding of HW TWDT.
        if (bms_queue_free_slots() == 0) {
            BMS_LOGE("BMS queue full (no free slots), stopping feeders and core1");
            s_allow_feeding = false;
        }

        /* Get one demo BMS sample */
        // Read one sample from BMS adapter
        esp_err_t err = bms->read_sample(&sample);
        // On success, push sample into inter-core queue
        if (err == ESP_OK) {
            // BMS_LOGI("Sample: ts=%lu ticks, cells=[%.3f, %.3f, %.3f, %.3f, %.3f] V, "
            //          "Vpack=%.3f V, I=%.3f A",
            //          (unsigned long)sample.timestamp,
            //          sample.cell_v[0], sample.cell_v[1], sample.cell_v[2],
            //          sample.cell_v[3], sample.cell_v[4],
            //          sample.pack_v, sample.pack_i);

            if (!bms_queue_push(&sample)) {
                BMS_LOGE("Failed to enqueue BMS sample (queue full or error)");
                /* On next iteration bms_queue_free_slots()==0 will trip and stop tasks */
            }
        } else {
            BMS_LOGE("BMS read_sample failed: %s", esp_err_to_name(err));
        }

        // End timing and check for real-time overrun
        end = xTaskGetTickCount();
        // If real-time period was exceeded, disable feeding of HW TWDT
        if ((end - start) > period) {
            BMS_LOGW("Core 1 RT overrun: %lu ms > %d ms",
                     (unsigned long)pdTICKS_TO_MS(end - start), CORE1_PERIOD_MS);
            s_allow_feeding = false;
        }

        // Puts task into blocked state for absolute period until next cycle (ensures real-time periodicity 20 Hz)
        vTaskDelayUntil(&last_wake, period);
    }
}


/// Core 1 TWDT feeder task. This task periodically feeds (resets) the hardware TWDT to prevent timeout.
/// Feeding is only performed if \ref s_allow_feeding flag is true, otherwise feeding is skipped, allowing TWDT to expire
/// and reset the system.
///
/// \param arg Unused in current implementation and may be NULL
/// \return None
static void core1_feeder_task(void *arg)
{
    // TODO: remove parameters if unused in future implementation
    (void)arg;

    // Register current task with TWDT. If registration fails, delete task.
    if (bms_wdt_register_current_task() != ESP_OK) {
        BMS_LOGE("Failed to register Core 1 feeder to TWDT");
        vTaskDelete(NULL);
    }

    const TickType_t feed_ticks = pdMS_TO_TICKS(WDT_FEED_MS);

    // Main Core 1 feeder loop. Periodically feed TWDT if allowed.
    while (1)
    {
        // Variable_allow_feeding indicates whether feeding is allowed. Variable can be set to false
        // by Core 0 or Core 1 tasks on error conditions.
        if (s_allow_feeding) {
            if (bms_wdt_feed_self() != ESP_OK) {
                BMS_LOGE("HW WD feed failed (Core 1 feeder)");
            }
        }
        // Put task into blocked state for defined period
        vTaskDelay(feed_ticks);
    }
}

/// Core 0 TWDT feeder task. This task periodically feeds (resets) the hardware TWDT to prevent timeout.
/// Feeding is only performed if \ref s_allow_feeding flag is true, otherwise feeding is skipped, allowing TWDT to expire
/// and reset the system.
///
/// \param arg Unused in current implementation and may be NULL
/// \return None
static void core0_feeder_task(void *arg)
{
    // TODO: remove parameters if unused in future implementation
    (void)arg;

    // Register current task with TWDT. If registration fails, delete task.
    if (bms_wdt_register_current_task() != ESP_OK) {
        BMS_LOGE("Failed to register Core 0 feeder to TWDT");
        vTaskDelete(NULL);
    }

    const TickType_t feed_ticks = pdMS_TO_TICKS(WDT_FEED_MS);

    // Main Core 0 feeder loop. Periodically feed TWDT if allowed.
    while (1)
    {
        // Variable_allow_feeding indicates whether feeding is allowed. Variable can be set to false
        // by Core 0 or Core 1 tasks on error conditions.
        if (s_allow_feeding) {
            if (bms_wdt_feed_self() != ESP_OK) {
                BMS_LOGE("HW WD feed failed (Core 0 feeder)");
            }
        }
        // Put task into blocked state for defined period
        vTaskDelay(feed_ticks);
    }
}
