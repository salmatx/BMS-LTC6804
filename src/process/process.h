/// Header file for `process.c`.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "bms_data.h"

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/
/// Maximum number of statistics windows that can be stored in bms_stats_buffer_t.
/// Value 5 corresponds to 1 second (1 s / 0.2 s) and is used if overvoltage/undervoltage violations are detected.
#define BMS_MAX_STATS_WINDOWS 5

/*==============================================================================================================*/
/*                                               Public Types                                                   */
/*==============================================================================================================*/
/// Structure defining one statistics one window computed from BMS samples. Calculated over 1 s or 0.2 s intervals.
typedef struct {
    TickType_t timestamp;                               ///< Timestamp containing earliest sample in this window
    size_t     sample_count;                            ///< Number of samples aggregated into this window

    float cell_v_avg[BMS_NUM_CELLS];                    ///< Average  per-cell voltages
    float cell_v_min[BMS_NUM_CELLS];                    ///< Minimum  per-cell voltages
    float cell_v_max[BMS_NUM_CELLS];                    ///< Maximum  per-cell voltages   

    float pack_v_avg;                                   ///< Average  pack voltage
    float pack_v_min;                                   ///< Minimum  pack voltage
    float pack_v_max;                                   ///< Maximum  pack voltage

    float pack_i_avg;                                   ///< Average  pack current
    float pack_i_min;                                   ///< Minimum  pack current
    float pack_i_max;                                   ///< Maximum  pack current

    uint16_t cell_errors;                               ///< Bitmask of limit violations
                                                        ///< 0x0001 – inspection bit (“valid data” marker) always set
                                                        ///< 0x0002 - cell 0 undervoltage, 0x0004 - cell 0 overvoltage,
                                                        ///< 0x0008 - cell 1 undervoltage, 0x0010 - cell 1 overvoltage,
                                                        ///< 0x0020 - cell 2 undervoltage, 0x0040 - cell 2 overvoltage,
                                                        ///< 0x0080 - cell 3 undervoltage, 0x0100 - cell 3 overvoltage,
                                                        ///< 0x0200 - cell 4 undervoltage, 0x0400 - cell 4 overvoltage,
                                                        ///< 0x0800 - pack undercurrent,   0x1000 - pack overcurrent
} bms_stats_t;

/// Structure defining buffer for storing multiple statistics windows.
typedef struct {
    bms_stats_t stats_array[BMS_MAX_STATS_WINDOWS];     ///< Array of statistics windows
    size_t      stats_count;                            ///< Number of valid statistics windows in the array
} bms_stats_buffer_t;

/*==============================================================================================================*/
/*                                             Public Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Variables                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                        Public Function Prototypes                                            */
/*==============================================================================================================*/
int bms_compute_stats(bms_sample_buffer_t *buf, bms_stats_buffer_t *out_stats);
void remove_processed_samples(bms_sample_buffer_t *buf, int sample_count);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/