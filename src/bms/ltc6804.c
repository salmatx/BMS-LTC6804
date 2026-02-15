/// LTC6804-2 multicell battery monitor ADC driver for ESP-IDF.
/// Communicates with the LTC6804-2 over SPI to measure individual cell voltages.
/// Ported from Linduino Arduino library -LTC68042.cpp (https://www.analog.com/en/products/ltc6804-1.html)
/// to ESP-IDF SPI master driver.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "ltc6804.h"
#include "logging.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include <string.h>

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag
#define LOG_MODULE_TAG "LTC6804"

/// Number of bytes received per register read (6 data + 2 PEC)
#define NUM_RX_BYTES   8

/// Number of data bytes in one register group
#define BYTES_IN_REG   6

/// ADC conversion delay for normal mode (in ms). Datasheet specifies ~2.3 ms for fast, ~3 ms for normal.
/// Using 10 ms provides safe margin for all modes.
#define ADC_CONV_DELAY_MS  10

/// LTC6804-2 addressed mode prefix
#define LTC6804_ADDR_CMD(addr)  (0x80 + ((addr) << 3))

/// Maximum number of read retries on PEC error before giving up
#define LTC6804_MAX_RETRIES    3

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static uint16_t pec15_calc(uint8_t len, const uint8_t *data);
static void     set_adc_cmd(uint8_t md, uint8_t dcp, uint8_t ch);
static esp_err_t spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len);
static void     cs_low(void);
static void     cs_high(void);
static void     wakeup_idle(void);
static void     wakeup_sleep(void);
static esp_err_t ltc6804_adcv(void);
static esp_err_t ltc6804_rdcv(uint16_t cell_codes[LTC6804_MAX_CELLS]);
static esp_err_t ltc6804_rdcv_reg(uint8_t reg, uint8_t *data);
static esp_err_t ltc6804_wrcfg(const uint8_t cfg[6]);
static esp_err_t ltc6804_rdcfg(uint8_t r_cfg[8]);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/
/// Pre-computed CRC15 lookup table for LTC6804 PEC calculation.
/// Sourced from LTC6804 datasheet / Linduino reference code.
static const uint16_t crc15_table[256] = {
    0x0000, 0xC599, 0xCEAB, 0x0B32, 0xD8CF, 0x1D56, 0x1664, 0xD3FD,
    0xF407, 0x319E, 0x3AAC, 0xFF35, 0x2CC8, 0xE951, 0xE263, 0x27FA,
    0xAD97, 0x680E, 0x633C, 0xA6A5, 0x7558, 0xB0C1, 0xBBF3, 0x7E6A,
    0x5990, 0x9C09, 0x973B, 0x52A2, 0x815F, 0x44C6, 0x4FF4, 0x8A6D,
    0x5B2E, 0x9EB7, 0x9585, 0x501C, 0x83E1, 0x4678, 0x4D4A, 0x88D3,
    0xAF29, 0x6AB0, 0x6182, 0xA41B, 0x77E6, 0xB27F, 0xB94D, 0x7CD4,
    0xF6B9, 0x3320, 0x3812, 0xFD8B, 0x2E76, 0xEBEF, 0xE0DD, 0x2544,
    0x02BE, 0xC727, 0xCC15, 0x098C, 0xDA71, 0x1FE8, 0x14DA, 0xD143,
    0xF3C5, 0x365C, 0x3D6E, 0xF8F7, 0x2B0A, 0xEE93, 0xE5A1, 0x2038,
    0x07C2, 0xC25B, 0xC969, 0x0CF0, 0xDF0D, 0x1A94, 0x11A6, 0xD43F,
    0x5E52, 0x9BCB, 0x90F9, 0x5560, 0x869D, 0x4304, 0x4836, 0x8DAF,
    0xAA55, 0x6FCC, 0x64FE, 0xA167, 0x729A, 0xB703, 0xBC31, 0x79A8,
    0xA8EB, 0x6D72, 0x6640, 0xA3D9, 0x7024, 0xB5BD, 0xBE8F, 0x7B16,
    0x5CEC, 0x9975, 0x9247, 0x57DE, 0x8423, 0x41BA, 0x4A88, 0x8F11,
    0x057C, 0xC0E5, 0xCBD7, 0x0E4E, 0xDDB3, 0x182A, 0x1318, 0xD681,
    0xF17B, 0x34E2, 0x3FD0, 0xFA49, 0x29B4, 0xEC2D, 0xE71F, 0x2286,
    0xA213, 0x678A, 0x6CB8, 0xA921, 0x7ADC, 0xBF45, 0xB477, 0x71EE,
    0x5614, 0x938D, 0x98BF, 0x5D26, 0x8EDB, 0x4B42, 0x4070, 0x85E9,
    0x0F84, 0xCA1D, 0xC12F, 0x04B6, 0xD74B, 0x12D2, 0x19E0, 0xDC79,
    0xFB83, 0x3E1A, 0x3528, 0xF0B1, 0x234C, 0xE6D5, 0xEDE7, 0x287E,
    0xF93D, 0x3CA4, 0x3796, 0xF20F, 0x21F2, 0xE46B, 0xEF59, 0x2AC0,
    0x0D3A, 0xC8A3, 0xC391, 0x0608, 0xD5F5, 0x106C, 0x1B5E, 0xDEC7,
    0x54AA, 0x9133, 0x9A01, 0x5F98, 0x8C65, 0x49FC, 0x42CE, 0x8757,
    0xA0AD, 0x6534, 0x6E06, 0xAB9F, 0x7862, 0xBDFB, 0xB6C9, 0x7350,
    0x51D6, 0x944F, 0x9F7D, 0x5AE4, 0x8919, 0x4C80, 0x47B2, 0x822B,
    0xA5D1, 0x6048, 0x6B7A, 0xAEE3, 0x7D1E, 0xB887, 0xB3B5, 0x762C,
    0xFC41, 0x39D8, 0x32EA, 0xF773, 0x248E, 0xE117, 0xEA25, 0x2FBC,
    0x0846, 0xCDDF, 0xC6ED, 0x0374, 0xD089, 0x1510, 0x1E22, 0xDBBB,
    0x0AF8, 0xCF61, 0xC453, 0x01CA, 0xD237, 0x17AE, 0x1C9C, 0xD905,
    0xFEFF, 0x3B66, 0x3054, 0xF5CD, 0x2630, 0xE3A9, 0xE89B, 0x2D02,
    0xA76F, 0x62F6, 0x69C4, 0xAC5D, 0x7FA0, 0xBA39, 0xB10B, 0x7492,
    0x5368, 0x96F1, 0x9DC3, 0x585A, 0x8BA7, 0x4E3E, 0x450C, 0x8095,
};

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// SPI device handle for the LTC6804
static spi_device_handle_t s_spi_dev = NULL;

/// Pre-computed ADCV command bytes (set by set_adc_cmd)
static uint8_t s_adcv_cmd[2];

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes the SPI bus and configures the LTC6804-2 chip.
/// Sets up ESP-IDF SPI master on the configured pins, adds the LTC6804 as a device,
/// configures ADC mode (normal, all channels, discharge disabled), and writes
/// the default configuration register.
///
/// \return ESP_OK on success, otherwise an error code
esp_err_t ltc6804_init(void)
{
    // Configure CS pin as GPIO output (manual control for isoSPI wakeup timing)
    gpio_config_t cs_cfg = {
        .pin_bit_mask  = (1ULL << LTC6804_PIN_CS),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_cfg);
    gpio_set_level(LTC6804_PIN_CS, 1); // CS idle high

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = LTC6804_PIN_MOSI,
        .miso_io_num   = LTC6804_PIN_MISO,
        .sclk_io_num   = LTC6804_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };

    esp_err_t ret = spi_bus_initialize(LTC6804_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        BMS_LOGE("SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add LTC6804 as SPI device — CS managed manually via GPIO
    spi_device_interface_config_t dev_cfg = {
        .mode           = 3,               // SPI Mode 3 (CPOL=1, CPHA=1) per LTC6804 datasheet
        .clock_speed_hz = LTC6804_SPI_FREQ_HZ,
        .spics_io_num   = -1,              // Manual CS control via GPIO
        .queue_size     = 1,
        .flags          = 0,
    };

    ret = spi_bus_add_device(LTC6804_SPI_HOST, &dev_cfg, &s_spi_dev);
    if (ret != ESP_OK) {
        BMS_LOGE("SPI add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set ADC command for normal mode, all cells, discharge disabled
    set_adc_cmd(LTC6804_MD_NORMAL, LTC6804_DCP_DISABLED, LTC6804_CH_ALL);

    // Write default configuration register:
    // CFGR0: GPIO pull-downs off, REFON=1 (keep reference powered), ADC mode bits = 0
    // CFGR1-CFGR3: undervoltage/overvoltage thresholds at 0 (disabled)
    // CFGR4-CFGR5: cell discharge switches all off
    uint8_t cfg[6] = {
        0xFE,   // CFGR0: GPIO1-5 pull-downs off, REFON=1, ADCOPT=0
        0x00,   // CFGR1: VUV[7:0]  — undervoltage threshold low byte
        0x00,   // CFGR2: VOV[3:0] | VUV[11:8]
        0x00,   // CFGR3: VOV[11:4]
        0x00,   // CFGR4: DCC1-DCC8 all off (no cell balancing)
        0x00,   // CFGR5: DCTO[3:0]=0, DCC9-DCC12 off
    };

    // Wake the LTC6804 from sleep state (long CS pulse)
    wakeup_sleep();

    ret = ltc6804_wrcfg(cfg);
    if (ret != ESP_OK) {
        BMS_LOGE("LTC6804 write config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    BMS_LOGI("LTC6804 ADC module initialized (SPI host %d, CS pin %d)", LTC6804_SPI_HOST, LTC6804_PIN_CS);

    // Diagnostic: read back configuration to verify SPI communication
    uint8_t r_cfg[8];
    ret = ltc6804_rdcfg(r_cfg);
    if (ret == ESP_OK) {
        BMS_LOGI("RDCFG OK: %02X %02X %02X %02X %02X %02X  PEC: %02X %02X",
                 r_cfg[0], r_cfg[1], r_cfg[2], r_cfg[3], r_cfg[4], r_cfg[5],
                 r_cfg[6], r_cfg[7]);
    } else {
        BMS_LOGW("RDCFG failed: %s  raw: %02X %02X %02X %02X %02X %02X %02X %02X",
                 esp_err_to_name(ret),
                 r_cfg[0], r_cfg[1], r_cfg[2], r_cfg[3],
                 r_cfg[4], r_cfg[5], r_cfg[6], r_cfg[7]);
    }

    return ESP_OK;
}

/// This function triggers an ADC conversion on the LTC6804, waits for completion,
/// reads back all cell voltage registers, and converts raw codes to voltages in volts.
///
/// \param[out] voltages   Array to receive cell voltages (in volts)
/// \param[in]  num_cells  Number of cells to populate (1..LTC6804_MAX_CELLS)
/// \return ESP_OK on success, ESP_ERR_INVALID_ARG on bad parameters, ESP_ERR_INVALID_CRC on PEC mismatch
esp_err_t ltc6804_read_cell_voltages(float *voltages, uint8_t num_cells)
{
    if (!voltages || num_cells == 0 || num_cells > LTC6804_MAX_CELLS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t cell_codes[LTC6804_MAX_CELLS];
    esp_err_t ret = ESP_FAIL;

    // Retry loop — SPI/PEC errors can be transient (noise, wakeup timing)
    for (int attempt = 0; attempt < LTC6804_MAX_RETRIES; ++attempt) {
        // Start ADC conversion for all cells
        ret = ltc6804_adcv();
        if (ret != ESP_OK) {
            continue;
        }

        // Wait for conversion to complete
        vTaskDelay(pdMS_TO_TICKS(ADC_CONV_DELAY_MS));

        // Read raw cell codes from all 4 register groups
        ret = ltc6804_rdcv(cell_codes);
        if (ret == ESP_OK) {
            break;
        }
    }

    if (ret != ESP_OK) {
        // Rate-limit warning: only log every 20th failure to avoid UART overload / WDT
        static uint32_t s_fail_cnt = 0;
        if ((++s_fail_cnt % 20) == 1) {
            BMS_LOGW("LTC6804 read failed (%lu total, last %d attempts): %s",
                     (unsigned long)s_fail_cnt, LTC6804_MAX_RETRIES, esp_err_to_name(ret));
        }
        return ret;
    }

    // Convert raw ADC codes to volts: each LSB = 100 µV = 0.0001 V
    for (uint8_t i = 0; i < num_cells; ++i) {
        voltages[i] = (float)cell_codes[i] * 0.0001f;
    }

    return ESP_OK;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/

/// This function calculates the CRC15/PEC15 used by the LTC6804 for data integrity verification.
/// Uses the pre-computed lookup table. The result is left-shifted by 1 (LSB is always 0).
///
/// \param len   Number of bytes in data
/// \param data  Pointer to data bytes
/// \return 16-bit PEC value (CRC15 * 2)
static uint16_t pec15_calc(uint8_t len, const uint8_t *data)
{
    uint16_t remainder = 16; // PEC seed per LTC6804 spec
    for (uint8_t i = 0; i < len; ++i) {
        uint16_t addr = ((remainder >> 7) ^ data[i]) & 0xFF;
        remainder = (remainder << 8) ^ crc15_table[addr];
    }
    return remainder * 2; // CRC15 has 0 in LSB
}

/// This function maps ADC mode, discharge control, and channel selection
/// into the 2-byte ADCV command stored in s_adcv_cmd.
///
/// \param md  ADC conversion mode (LTC6804_MD_FAST / NORMAL / FILTERED)
/// \param dcp Discharge control (LTC6804_DCP_DISABLED / ENABLED)
/// \param ch  Cell channel selection (LTC6804_CH_ALL .. LTC6804_CH_6_AND_12)
static void set_adc_cmd(uint8_t md, uint8_t dcp, uint8_t ch)
{
    uint8_t md_bits;

    // ADCV command encoding per LTC6804 datasheet:
    // Byte 0: 0x02 + MD[1]
    // Byte 1: MD[0]<<7 + 0x60 + DCP<<4 + CH[2:0]
    md_bits = (md & 0x02) >> 1;
    s_adcv_cmd[0] = md_bits + 0x02;

    md_bits = (md & 0x01) << 7;
    s_adcv_cmd[1] = md_bits + 0x60 + (dcp << 4) + ch;
}

/// This function performs an SPI transfer (simultaneous TX and RX).
/// CS is NOT managed here — caller must assert/deassert CS via cs_low()/cs_high().
///
/// \param tx   TX buffer (may be NULL for RX-only)
/// \param rx   RX buffer (may be NULL for TX-only)
/// \param len  Number of bytes to transfer
/// \return ESP_OK on success
static esp_err_t spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t txn = {
        .length    = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
        .rxlength  = rx ? (len * 8) : 0,
    };
    return spi_device_transmit(s_spi_dev, &txn);
}

/// Pull CS low (active)
static void cs_low(void)
{
    gpio_set_level(LTC6804_PIN_CS, 0);
}

/// Pull CS high (idle)
static void cs_high(void)
{
    gpio_set_level(LTC6804_PIN_CS, 1);
}

/// This function wakes up the LTC6804 isoSPI interface from idle state.
/// Holds CS low for at least 10 µs then releases, matching the Arduino reference code.
static void wakeup_idle(void)
{
    cs_low();
    ets_delay_us(10);  // tWAKE idle: min 6.7 µs per datasheet
    cs_high();
    ets_delay_us(10);  // Brief settling time before communication
}

/// This function wakes up the LTC6804 from deep sleep state.
/// Requires CS low for at least 300 µs (tWAKE sleep per datasheet).
static void wakeup_sleep(void)
{
    cs_low();
    ets_delay_us(1000); // 1 ms — generous margin for sleep wakeup
    cs_high();
    ets_delay_us(10);
}

/// This function sends the ADCV (start cell voltage conversion) command to the LTC6804
/// in addressed mode.
///
/// \return ESP_OK on success
static esp_err_t ltc6804_adcv(void)
{
    uint8_t cmd[4];

    wakeup_idle();

    // Build broadcast ADCV command (matching Arduino reference — works for both -1 and -2 variants)
    cmd[0] = s_adcv_cmd[0];
    cmd[1] = s_adcv_cmd[1];

    // Calculate and append PEC
    uint16_t pec = pec15_calc(2, cmd);
    cmd[2] = (uint8_t)(pec >> 8);
    cmd[3] = (uint8_t)(pec);

    // Send with manual CS
    cs_low();
    esp_err_t ret = spi_transfer(cmd, NULL, 4);
    cs_high();
    return ret;
}

/// This function reads one cell voltage register group from the LTC6804 in addressed mode.
/// Each register group contains 3 cell voltages (6 data bytes) + 2 PEC bytes = 8 bytes total.
///
/// \param reg   Register group number (1=A, 2=B, 3=C, 4=D)
/// \param data  Buffer of at least NUM_RX_BYTES (8) bytes to receive raw data
/// \return ESP_OK on success
static esp_err_t ltc6804_rdcv_reg(uint8_t reg, uint8_t *data)
{
    // Command byte 1 selects the register group: A=0x04, B=0x06, C=0x08, D=0x0A
    static const uint8_t reg_cmd[4] = {0x04, 0x06, 0x08, 0x0A};

    if (reg < 1 || reg > 4) {
        return ESP_ERR_INVALID_ARG;
    }

    // Build addressed read command (LTC6804-2 addressed mode)
    uint8_t cmd[4];
    cmd[0] = LTC6804_ADDR_CMD(LTC6804_IC_ADDR);
    cmd[1] = reg_cmd[reg - 1];

    uint16_t pec = pec15_calc(2, cmd);
    cmd[2] = (uint8_t)(pec >> 8);
    cmd[3] = (uint8_t)(pec);

    // Full-duplex: send 4-byte command + clock 8 bytes of response
    uint8_t tx_buf[4 + NUM_RX_BYTES];
    uint8_t rx_buf[4 + NUM_RX_BYTES];

    memcpy(tx_buf, cmd, 4);
    memset(&tx_buf[4], 0xFF, NUM_RX_BYTES); // Dummy bytes to clock in response

    wakeup_idle();

    cs_low();
    esp_err_t ret = spi_transfer(tx_buf, rx_buf, sizeof(tx_buf));
    cs_high();

    if (ret != ESP_OK) {
        return ret;
    }

    // Response data starts after the 4-byte command phase
    memcpy(data, &rx_buf[4], NUM_RX_BYTES);
    return ESP_OK;
}

/// This function reads all four cell voltage register groups (A–D) from the LTC6804,
/// parses the 16-bit raw cell codes, and verifies the PEC for each group.
///
/// \param[out] cell_codes  Array of LTC6804_MAX_CELLS uint16_t values (raw ADC codes)
/// \return ESP_OK on success, ESP_ERR_INVALID_CRC if any register PEC check fails
static esp_err_t ltc6804_rdcv(uint16_t cell_codes[LTC6804_MAX_CELLS])
{
    uint8_t reg_data[NUM_RX_BYTES];
    int pec_errors = 0;

    for (uint8_t reg = 1; reg <= LTC6804_NUM_CV_REG; ++reg) {
        esp_err_t ret = ltc6804_rdcv_reg(reg, reg_data);
        if (ret != ESP_OK) {
            return ret;
        }

        // Parse 3 cell voltages from the 6 data bytes (little-endian 16-bit)
        for (uint8_t cell = 0; cell < LTC6804_CELLS_PER_REG; ++cell) {
            uint8_t idx = cell * 2;
            uint16_t raw = reg_data[idx] | ((uint16_t)reg_data[idx + 1] << 8);
            cell_codes[cell + (reg - 1) * LTC6804_CELLS_PER_REG] = raw;
        }

        // Verify PEC: received PEC is bytes 6-7 (big-endian), calculated over bytes 0-5
        uint16_t received_pec = ((uint16_t)reg_data[6] << 8) | reg_data[7];
        uint16_t calc_pec = pec15_calc(BYTES_IN_REG, reg_data);
        if (received_pec != calc_pec) {
            pec_errors++;
        }
    }

    if (pec_errors > 0) {
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

/// This function writes the 6-byte configuration register to the LTC6804 in addressed mode.
///
/// \param cfg  Pointer to 6 configuration bytes (CFGR0 .. CFGR5)
/// \return ESP_OK on success
static esp_err_t ltc6804_wrcfg(const uint8_t cfg[6])
{
    // WRCFG command with LTC6804-2 addressing
    uint8_t cmd[4];
    cmd[0] = LTC6804_ADDR_CMD(LTC6804_IC_ADDR);
    cmd[1] = 0x01;  // WRCFG register command

    uint16_t cmd_pec = pec15_calc(2, cmd);
    cmd[2] = (uint8_t)(cmd_pec >> 8);
    cmd[3] = (uint8_t)(cmd_pec);

    // Build data payload: 6 config bytes + 2 PEC bytes
    uint8_t payload[8];
    memcpy(payload, cfg, 6);
    uint16_t data_pec = pec15_calc(6, cfg);
    payload[6] = (uint8_t)(data_pec >> 8);
    payload[7] = (uint8_t)(data_pec);

    // Combine into single TX: 4-byte cmd + 8-byte payload
    uint8_t tx_buf[12];
    memcpy(tx_buf, cmd, 4);
    memcpy(&tx_buf[4], payload, 8);

    wakeup_idle();

    cs_low();
    esp_err_t ret = spi_transfer(tx_buf, NULL, sizeof(tx_buf));
    cs_high();
    return ret;
}

/// This function reads back the 6-byte configuration register + 2 PEC bytes from the LTC6804
/// in addressed mode. Used for diagnostics to verify SPI communication.
///
/// \param r_cfg  Buffer of at least 8 bytes to receive CFGR0..CFGR5 + PEC_H + PEC_L
/// \return ESP_OK if PEC matches, ESP_ERR_INVALID_CRC otherwise
static esp_err_t ltc6804_rdcfg(uint8_t r_cfg[8])
{
    uint8_t cmd[4];
    cmd[0] = LTC6804_ADDR_CMD(LTC6804_IC_ADDR);
    cmd[1] = 0x02;  // RDCFG command

    uint16_t pec = pec15_calc(2, cmd);
    cmd[2] = (uint8_t)(pec >> 8);
    cmd[3] = (uint8_t)(pec);

    uint8_t tx_buf[4 + 8];
    uint8_t rx_buf[4 + 8];
    memcpy(tx_buf, cmd, 4);
    memset(&tx_buf[4], 0xFF, 8);

    wakeup_idle();

    cs_low();
    esp_err_t ret = spi_transfer(tx_buf, rx_buf, sizeof(tx_buf));
    cs_high();

    if (ret != ESP_OK) {
        return ret;
    }

    memcpy(r_cfg, &rx_buf[4], 8);

    // Verify PEC on the 6 data bytes
    uint16_t received_pec = ((uint16_t)r_cfg[6] << 8) | r_cfg[7];
    uint16_t calc_pec = pec15_calc(6, r_cfg);
    if (received_pec != calc_pec) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}
