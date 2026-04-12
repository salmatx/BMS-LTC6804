/// Header file for `bms_configuration.c`.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once

#include <stdint.h>
#include <stdbool.h>

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                               Public Types                                                   */
/*==============================================================================================================*/
/// BMS adapter mode
typedef enum {
    BMS_ADAPTER_LTC6804 = 0,  ///< LTC6804 hardware adapter (default)
    BMS_ADAPTER_DEMO    = 1,  ///< Demo adapter with random data
} bms_adapter_mode_t;

/// Structure defining BMS configuration parameters for measured battery pack
typedef struct {
    bms_adapter_mode_t adapter_mode;    ///< Selected BMS adapter (ltc6804 or demo)
    uint8_t num_cells;                  ///< Number of cells in the battery pack (runtime configurable)
    bool    current_enable;             ///< Enable current measurement (true = measure, false = skip)
    bool    temperature_enable;         ///< Enable temperature measurement (true = measure, false = skip)
    float   cell_v_min;                 ///< Minimum per-cell voltage
    float   cell_v_max;                 ///< Maximum per-cell voltage
    float   pack_v_min;                 ///< Minimum total pack voltage
    float   pack_v_max;                 ///< Maximum total pack voltage
    float   series_pack_i_min;          ///< Minimum current for cells in series
    float   series_pack_i_max;          ///< Maximum current for cells in series
} bms_config_t;

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