/// Header file providing data structures for BMS measurements and sample buffering.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/
/// Number of cells in the battery pack
#define BMS_NUM_CELLS 5

/*==============================================================================================================*/
/*                                               Public Types                                                   */
/*==============================================================================================================*/
/// Structure defining one measured BMS sample.
typedef struct {
    float      cell_v[BMS_NUM_CELLS];  ///< per-cell voltages
    float      pack_v;                 ///< sum of cells
    float      pack_i;                 ///< pack current
    TickType_t timestamp;              ///< RTOS ticks
} bms_sample_t;

/// Structure defining ring buffer for storing measured BMS samples.
typedef struct {
    bms_sample_t *samples;   ///< array containing samples
    size_t        head;      ///< index of first valid sample
    size_t        count;     ///< number of valid entries
    size_t        capacity;  ///< allocated size
} bms_sample_buffer_t;

/*==============================================================================================================*/
/*                                             Public Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Variables                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                        Public Function Prototypes                                            */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/
/// This function computes the buffer index for given offset from head. When offset exceeds capacity, wraps around.
/// If offset is substituted with buf->count, gives index of last valid sample.
///
/// \param buf Pointer to ring buffer structure
/// \param offset Offset from head
/// \return Computed buffer index
static inline size_t bms_buf_index(const bms_sample_buffer_t *buf, size_t offset)
{
    return (buf->head + offset) % buf->capacity;
}