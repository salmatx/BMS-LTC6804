/// Header file for system telemetry module.
/// Provides ESP32 and BMS device telemetry including device identification, system stats, and hardware status.

/*==============================================================================================================*/
/*                                                 Includes                                                     */
/*==============================================================================================================*/
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*==============================================================================================================*/
/*                                               Public Macros                                                  */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                               Public Types                                                   */
/*==============================================================================================================*/
/// Structure containing ESP32 system telemetry data
typedef struct {
    char device_id[18];         ///< Device unique ID (MAC address as string)
    char sw_version[32];        ///< Software version (git hash)
    uint8_t cpu_load;           ///< CPU load percentage (0-100)
    uint32_t free_heap;         ///< Free heap memory in bytes
    uint32_t min_free_heap;     ///< Minimum free heap since boot in bytes
} esp32_telemetry_t;

/// Structure containing LTC6804 status registers
typedef struct {
    uint16_t status_a;          ///< Status register A
    uint16_t status_b;          ///< Status register B
    bool valid;                 ///< True if status registers were read successfully
} ltc6804_status_t;

/*==============================================================================================================*/
/*                                             Public Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                             Public Variables                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                        Public Function Prototypes                                            */
/*==============================================================================================================*/
void telemetry_init(void);
void telemetry_get_device_id(char *id_buf, size_t buf_size);
void telemetry_get_sw_version(char *ver_buf, size_t buf_size);
void telemetry_get_esp32_telemetry(esp32_telemetry_t *telem);
void telemetry_get_ltc6804_status(ltc6804_status_t *status);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/
