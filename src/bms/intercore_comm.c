/// This module implements inter-core communication for BMS samples using FreeRTOS queues. It is used to pass
/// BMS samples from Core 1 (real-time processing) to Core 0 (non-real-time communication).

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "intercore_comm.h"
#include "esp_log.h"
#include "logging.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "INTERCORE_COMM"

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
/// Initialized queue handle to NULL
QueueHandle_t g_bms_queue = NULL;

/// Initialized spinlock as unlocked
portMUX_TYPE g_bms_queue_spinlock = portMUX_INITIALIZER_UNLOCKED;

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes the inter-core BMS sample queue. Creates FreeRTOS queue with length
/// defined by BMS_QUEUE_LEN.
///
/// \param None
/// \return None
void bms_queue_init(void)
{
    g_bms_queue = xQueueCreate(BMS_QUEUE_LEN, sizeof(bms_sample_t));
    if (!g_bms_queue) {
        BMS_LOGE("Failed to create BMS queue (len=%d)", BMS_QUEUE_LEN);
    }
}

/// This function pushes one BMS sample into the inter-core queue. If the queue is full, the sample is dropped.
/// This function is intended to be called from Core 1 (producer).
///
/// \param sample Pointer to BMS sample to push
/// \return true if sample was successfully pushed, false if queue was full or error occurred
bool bms_queue_push(const bms_sample_t *sample)
{
    if (!g_bms_queue || !sample) {
        return false;
    }

    bool ok = false;

    taskENTER_CRITICAL(&g_bms_queue_spinlock);
    UBaseType_t free_slots = uxQueueSpacesAvailable(g_bms_queue);  // 0 => full [web:107][web:119]

    if (free_slots > 0) {
        if (xQueueSendToBack(g_bms_queue, sample, 0) == pdPASS) {
            ok = true;
        }
    }
    taskEXIT_CRITICAL(&g_bms_queue_spinlock);

    return ok;
}

/// This function pops one BMS sample from the inter-core queue. If the queue is empty, no sample is returned.
/// This function is intended to be called from Core 0 (consumer).
///
/// \param out Pointer to BMS sample structure to fill
/// \return true if sample was successfully popped, false if queue was empty or error occurred
bool bms_queue_pop(bms_sample_t *out)
{
    if (!g_bms_queue || !out) {
        return 0;
    }

    bool pop_successful = false;

    taskENTER_CRITICAL(&g_bms_queue_spinlock);
    if (uxQueueMessagesWaiting(g_bms_queue) > 0) {
        if (xQueueReceive(g_bms_queue, out, 0) == pdPASS) {
            pop_successful = true;
        }
    }
    taskEXIT_CRITICAL(&g_bms_queue_spinlock);

    return pop_successful;
}

/// This function returns the number of free slots in the inter-core BMS sample queue.
///
/// \param None
/// \return Number of free slots in the queue, or 0 if queue is not initialized
UBaseType_t bms_queue_free_slots(void)
{
    if (!g_bms_queue) {
        return 0;
    }
    taskENTER_CRITICAL(&g_bms_queue_spinlock);
    UBaseType_t free_slots = uxQueueSpacesAvailable(g_bms_queue);
    taskEXIT_CRITICAL(&g_bms_queue_spinlock);
    return free_slots;
}

/// This function returns the number of items currently stored in the inter-core BMS sample queue.
///
/// \param None
/// \return Number of items in the queue, or 0 if queue is not initialized
UBaseType_t bms_queue_items_waiting(void)
{
    if (!g_bms_queue) {
        return 0;
    }
    taskENTER_CRITICAL(&g_bms_queue_spinlock);
    UBaseType_t items = uxQueueMessagesWaiting(g_bms_queue);
    taskEXIT_CRITICAL(&g_bms_queue_spinlock);
    return items;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/