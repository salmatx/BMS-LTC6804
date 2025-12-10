/// Header file for `bms_intercore_comm.c`.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"
#include "bms_data.h"

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/
/// Count of seconds to store in BMS sample queue
#define BMS_QUEUE_SECONDS   30
/// The rare of storing BMS samples in the queue (in Hz)
#define BMS_QUEUE_RATE_HZ   20
/// Queue length: store 30 seconds of BMS samples at 20 Hz rate (30 * 20 = 600)
#define BMS_QUEUE_LEN       (BMS_QUEUE_SECONDS * BMS_QUEUE_RATE_HZ)

/*==============================================================================================================*/
/*                                               Public Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Variables                                                 */
/*==============================================================================================================*/
/// Queue handle for BMS samples used for inter-core communication
extern QueueHandle_t g_bms_queue;

/// Spinlock for protecting BMS queue access across cores. Used to engage critical sections.
extern portMUX_TYPE g_bms_queue_spinlock;

/*==============================================================================================================*/
/*                                        Public Function Prototypes                                            */
/*==============================================================================================================*/
void bms_queue_init(void);
bool bms_queue_push(const bms_sample_t *sample);
bool bms_queue_pop(bms_sample_t *out);

UBaseType_t bms_queue_free_slots(void);
UBaseType_t bms_queue_items_waiting(void);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/