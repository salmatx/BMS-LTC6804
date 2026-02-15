/// Header file for LTC6804-2 multicell battery monitor ADC driver module.
/// Provides SPI-based communication with the LTC6804-2 chip for measuring individual cell voltages.

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

/// SPI pin assignments — adjust to match your hardware wiring
/// GPIO 23/19/18/5 are native VSPI (SPI3_HOST) pins on ESP32
#define LTC6804_SPI_HOST       SPI3_HOST
#define LTC6804_PIN_MOSI       23
#define LTC6804_PIN_MISO       19
#define LTC6804_PIN_SCLK       18
#define LTC6804_PIN_CS         5

/// SPI clock frequency (1 MHz is safe for LTC6804 isoSPI)
#define LTC6804_SPI_FREQ_HZ   1000000

/// LTC6804-2 IC address (0-based). In a single-IC setup this is typically 0.
#define LTC6804_IC_ADDR        0

/// ADC conversion speed modes
#define LTC6804_MD_FAST        1   ///< Fast conversion (27 kHz filter)
#define LTC6804_MD_NORMAL      2   ///< Normal conversion (7 kHz filter)
#define LTC6804_MD_FILTERED    3   ///< Filtered conversion (26 Hz filter)

/// Channel selection for ADC conversion command. Defines which cells are converted in the next ADC cycle.
#define LTC6804_CH_ALL         0   ///< Convert all cells
#define LTC6804_CH_1_AND_7     1
#define LTC6804_CH_2_AND_8     2
#define LTC6804_CH_3_AND_9     3
#define LTC6804_CH_4_AND_10    4
#define LTC6804_CH_5_AND_11    5
#define LTC6804_CH_6_AND_12    6

/// Discharge control for ADC conversion command. Defines whether cell discharge is permitted during ADC conversion.
#define LTC6804_DCP_DISABLED   0   ///< Discharge not permitted during conversion
#define LTC6804_DCP_ENABLED    1   ///< Discharge permitted during conversion

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
esp_err_t ltc6804_init(void);
esp_err_t ltc6804_read_cell_voltages(float *voltages, uint8_t num_cells);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/
