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
/// Maximum length of reset message string in telemetry
#define RESET_MSG_MAXLEN 700

/// Structure containing ESP32 system telemetry data
typedef struct {
    char device_id[18];         ///< Device unique ID (MAC address as string)
    char sw_version[32];        ///< Software version (git hash)
    uint8_t cpu_load;           ///< CPU load percentage (0-100)
    uint32_t free_heap;         ///< Free heap memory in bytes
    uint32_t min_free_heap;     ///< Minimum free heap since boot in bytes
    uint8_t reset_reason;       ///< Last reset reason (esp_reset_reason_t)
    char reset_msg[RESET_MSG_MAXLEN]; ///< Last 5 error logs before TWDT reset (empty if other reset reason)
} esp32_telemetry_t;

/// Structure containing LTC6804 status registers (Table 43 & Table 44)
typedef struct {
    uint16_t soc;           ///< Sum of cells voltage raw ADC (STATA bytes 0-1, LSB = 100µV × 20)
    uint16_t itmp;          ///< Internal die temperature raw ADC (STATA bytes 2-3)
    uint16_t va;            ///< Analog power supply voltage raw ADC (STATA bytes 4-5, LSB = 100µV)
    uint16_t vd;            ///< Digital power supply voltage raw ADC (STATB bytes 0-1, LSB = 100µV)
    uint32_t cell_flags;    ///< Cell UV/OV flags 24-bit (STATB bytes 2-4)
                            ///< Bit layout per byte: CnOV CnUV ... C1OV C1UV
    uint8_t  diag;          ///< Diagnostic byte (STATB byte 5): REV[7:4] RSVD[3:2] MUXFAIL[1] THSD[0]
    bool     valid;         ///< True if status registers were read successfully
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
void telemetry_update_ltc6804_status(const uint8_t stata[6], const uint8_t statb[6], bool valid);

/*==============================================================================================================*/
/*                                          Public Inline Functions                                             */
/*==============================================================================================================*/
