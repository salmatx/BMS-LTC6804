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

/// Number of GPIO channels available on the LTC6804
#define LTC6804_NUM_GPIO       5

/// Number of auxiliary register groups (A, B)
#define LTC6804_NUM_AUX_REG    2

/// Number of auxiliary values per register group
#define LTC6804_AUX_PER_REG   3

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

/// GPIO/auxiliary channel selection for ADAX conversion command.
#define LTC6804_AUX_CH_ALL     0   ///< Convert all GPIOs and 2nd reference
#define LTC6804_AUX_CH_GPIO1   1   ///< Convert GPIO1 only
#define LTC6804_AUX_CH_GPIO2   2   ///< Convert GPIO2 only
#define LTC6804_AUX_CH_GPIO3   3   ///< Convert GPIO3 only
#define LTC6804_AUX_CH_GPIO4   4   ///< Convert GPIO4 only
#define LTC6804_AUX_CH_GPIO5   5   ///< Convert GPIO5 only
#define LTC6804_AUX_CH_VREF2   6   ///< Convert Vref2 only

/// Discharge control for ADC conversion command. Defines whether cell discharge is permitted during ADC conversion.
#define LTC6804_DCP_DISABLED   0   ///< Discharge not permitted during conversion
#define LTC6804_DCP_ENABLED    1   ///< Discharge permitted during conversion

/// Current sense configuration — adjust to match your hardware setup.
/// GPIO channel connected to the current sensor output.
#define LTC6804_CURRENT_GPIO       1

/// Current sensor sensitivity in V/A.
/// Example: 50A LEM sensor with ±1.667V output range → 1.667/50 = 0.03334 V/A
#define LTC6804_CURRENT_SENSITIVITY  0.03334f

/// Voltage output of the current sensor at zero current (offset / bias).
/// Bidirectional sensors (e.g. hall effect) typically output Vcc/2 ≈ 2.5V at zero current.
#define LTC6804_CURRENT_OFFSET_V     2.5f

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
esp_err_t ltc6804_read_gpio_voltages(float *voltages, uint8_t num_gpio);
esp_err_t ltc6804_read_current(float *current_amps);
esp_err_t ltc6804_read_status(uint8_t stata[6], uint8_t statb[6]);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/
