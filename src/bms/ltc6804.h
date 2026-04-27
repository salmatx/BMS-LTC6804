/// Header file for LTC6804-2 multicell battery monitor ADC driver module.
/// Provides SPI-based communication with the LTC6804-2 for measuring individual cell voltages.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once

#include "esp_err.h"
#include <stdint.h>

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/
/// Maximum number of cells the LTC6804 can measure
#define LTC6804_MAX_CELLS      12

/// Number of cell voltage register groups (A, B, C, D)
#define LTC6804_NUM_CV_REG     4

/// Number of cells per register group
#define LTC6804_CELLS_PER_REG  3

/// SPI pin assignments. GPIO 23/19/18/5 are native SPI pins on ESP32
/// SPI HOST for ESP32 SPI master driver
#define LTC6804_SPI_HOST       SPI3_HOST
/// SPI MOSI pin
#define LTC6804_PIN_MOSI       23
/// SPI MISO pin
#define LTC6804_PIN_MISO       19
/// SPI SCLK pin
#define LTC6804_PIN_SCLK       18
/// SPI CS pin, can be any GPIO since CS is controlled manually via GPIO functions (cs_low() and cs_high())
#define LTC6804_PIN_CS         5

/// SPI clock frequency 1 MHz. LTC6804-2 supports up to 10 MHz. Using 1 MHz provides safe margin for signal integrity and timing
#define LTC6804_SPI_FREQ_HZ   1000000

/// LTC6804-2 IC address. In a single-IC setup this is typically 0.
#define LTC6804_IC_ADDR        0

/// ADC conversion fast mode (27 kHz filter)
#define LTC6804_MD_FAST        1
/// ADC conversion normal mode (7 kHz filter)
#define LTC6804_MD_NORMAL      2
/// ADC conversion filtered mode (26 Hz filter)
#define LTC6804_MD_FILTERED    3

/// Channel selection for ADC conversion command. Convert all cell channels (C1-C12)
#define LTC6804_CH_ALL         0
/// Channel selection for ADC conversion command. Convert only cell channels C1 and C7
#define LTC6804_CH_1_AND_7     1
/// Channel selection for ADC conversion command. Convert only cell channels C2 and C8
#define LTC6804_CH_2_AND_8     2
/// Channel selection for ADC conversion command. Convert only cell channels C3 and C9
#define LTC6804_CH_3_AND_9     3
/// Channel selection for ADC conversion command. Convert only cell channels C4 and C10
#define LTC6804_CH_4_AND_10    4
/// Channel selection for ADC conversion command. Convert only cell channels C5 and C11
#define LTC6804_CH_5_AND_11    5
/// Channel selection for ADC conversion command. Convert only cell channels C6 and C12
#define LTC6804_CH_6_AND_12    6

/// Discharge control for ADC conversion command. Defines whether cell discharge is permitted during ADC conversion.
/// Discharge not permitted during conversion.
#define LTC6804_DCP_DISABLED   0
/// Discharge control for ADC conversion command. Defines whether cell discharge is permitted during ADC conversion.
/// Discharge permitted during conversion.
#define LTC6804_DCP_ENABLED    1

/*==============================================================================================================*/
/*                                               Public Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Variables                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                        Public Function Prototypes                                            */
/*==============================================================================================================*/
esp_err_t ltc6804_init(float cell_v_min, float cell_v_max);
esp_err_t ltc6804_read_cell_voltages(float *voltages, uint8_t num_cells);
esp_err_t ltc6804_read_status(uint8_t stata[6], uint8_t statb[6]);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/
