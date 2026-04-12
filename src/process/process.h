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

    float cell_v_avg[BMS_MAX_CELLS];                    ///< Average  per-cell voltages

    float pack_v_avg;                                   ///< Average  pack voltage

    float pack_i_avg;                                   ///< Average  pack current

    float temperature_avg;                               ///< Average  temperature (deg C)

    uint32_t cell_errors;                               ///< Bitmask of limit violations
                                                        ///< Bit  0:       inspection bit ("valid data" marker) always set
                                                        ///< Bit  1- 2:    cell  0 undervoltage / overvoltage
                                                        ///< Bit  3- 4:    cell  1 undervoltage / overvoltage
                                                        ///< Bit  5- 6:    cell  2 undervoltage / overvoltage
                                                        ///< Bit  7- 8:    cell  3 undervoltage / overvoltage
                                                        ///< Bit  9-10:    cell  4 undervoltage / overvoltage
                                                        ///< Bit 11-12:    cell  5 undervoltage / overvoltage
                                                        ///< Bit 13-14:    cell  6 undervoltage / overvoltage
                                                        ///< Bit 15-16:    cell  7 undervoltage / overvoltage
                                                        ///< Bit 17-18:    cell  8 undervoltage / overvoltage
                                                        ///< Bit 19-20:    cell  9 undervoltage / overvoltage
                                                        ///< Bit 21-22:    cell 10 undervoltage / overvoltage
                                                        ///< Bit 23-24:    cell 11 undervoltage / overvoltage
                                                        ///< Bit 25:       pack undercurrent
                                                        ///< Bit 26:       pack overcurrent
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