/// Module providing adapter interface for getting BMS data samples.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "bms_adapter.h"
#include "ltc6804.h"
#include "configuration.h"
#include "logging.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_random.h"
#include <stdlib.h>
#include <math.h>

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "BMS_ADAPTER"

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static uint32_t demo_rand32(void);
static float demo_rand01(void);
static esp_err_t demo_init(void);
static esp_err_t demo_read_sample(bms_sample_t *out);
static esp_err_t ltc6804_adapter_init(void);
static esp_err_t ltc6804_adapter_read_sample(bms_sample_t *out);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/
/// Selected adapter instance
static const bms_adapter_t *s_current_adapter = NULL;

/// Demo adapter instance
static const bms_adapter_t s_demo_adapter = {
    .init        = demo_init,
    .read_sample = demo_read_sample,
};

/// LTC6804 hardware adapter instance
static const bms_adapter_t s_ltc6804_adapter = {
    .init        = ltc6804_adapter_init,
    .read_sample = ltc6804_adapter_read_sample,
};

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function selects and initializes the demo BMS adapter.
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_demo_adapter_select(void)
{
    s_current_adapter = &s_demo_adapter;
    return s_current_adapter->init();
}

/// This function returns a pointer to the currently selected BMS adapter.
/// \param None
/// \return Pointer to the selected BMS adapter
const bms_adapter_t *bms_get_adapter(void)
{
    return s_current_adapter;
}

/// This function selects and initializes the LTC6804 hardware BMS adapter.
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t bms_ltc6804_adapter_select(void)
{
    s_current_adapter = &s_ltc6804_adapter;
    return s_current_adapter->init();
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// This function returns a pseudo-random 32-bit unsigned integer using xorshift32 algorithm. Function is used to
/// generate demo BMS samples.
///
/// \param None
/// \return Pseudo-random 32-bit unsigned integer
static uint32_t demo_rand32(void)
{
    static uint32_t state = 0;

    // Initialize state on first call
    if (state == 0) {
        // Seed state with hardware random number
        esp_fill_random(&state, sizeof(state));
        if (state == 0) {
            state = 0x12345678u;
        }
    }

    // Xorshift32 algorithm
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

/// This function returns a pseudo-random float in the range [0, 1). Function is used to
/// generate demo BMS samples.
///
/// \param None
/// \return Pseudo-random float in [0, 1)
static float demo_rand01(void)
{
    // Get 24 random bits and scale to [0, 1)
    return (demo_rand32() & 0xFFFFFFu) / (float)0x1000000u;
}

/// This function initializes the demo BMS adapter. Function just logs initialization message and will
/// be in future replaced with real adapter init.
///
/// \param None
/// \return ESP_OK on success.
static esp_err_t demo_init(void)
{
    BMS_LOGI("Demo BMS adapter initialized (random cell voltages)");
    return ESP_OK;
}

/* Generate one demo sample with random cells and occasional under/over-voltage */
/// This function reads one demo BMS sample. Current implementation generates random per-cell voltages
/// within configured limits, with occasional under-voltage and over-voltage events. Pack current is
/// also generated randomly within configured limits. In future implementation, this function will read real data
/// from BMS.
///
/// \param out Pointer to output sample structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t demo_read_sample(bms_sample_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    // Probabilities of under-voltage event.
    const float p_underv = 0.02f;
    // Probability of over-voltage event.
    const float p_overv  = 0.02f;

    // Voltage of whole battery pack
    float pack_v = 0.0f;

    for (int i = 0; i < g_cfg.battery.num_cells; ++i) {
        // Generate voltage withing configured min/max limits
        float r = demo_rand01();
        float v = g_cfg.battery.cell_v_min + r * (g_cfg.battery.cell_v_max - g_cfg.battery.cell_v_min);

        // Generates under-voltage or over-voltage based on defined probabilities
        float e = demo_rand01();
        if (e < p_underv) {
            float drop = 0.1f + demo_rand01() * 0.2f;  // 0.1–0.3 V down
            v -= drop;
        } else if (e > 1.0f - p_overv) {
            float raise = 0.1f + demo_rand01() * 0.2f; // 0.1–0.3 V up
            v += raise;
        }

        out->cell_v[i] = v;
        pack_v += v;
    }

    out->pack_v = pack_v;

    // Generate random pack current within configured min/max limits (only if current measurement enabled)
    if (g_cfg.battery.current_enable) {
        float ir = demo_rand01();
        out->pack_i = g_cfg.battery.current_min + ir * (g_cfg.battery.current_max - g_cfg.battery.current_min);
    } else {
        out->pack_i = 0.0f;
    }

    // Set timestamp
    out->timestamp = xTaskGetTickCount();

    return ESP_OK;
}

/// This function initializes the LTC6804 hardware adapter by calling the LTC6804 ADC module init.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
static esp_err_t ltc6804_adapter_init(void)
{
    esp_err_t ret = ltc6804_init(g_cfg.battery.cell_v_min, g_cfg.battery.cell_v_max);
    if (ret != ESP_OK) {
        BMS_LOGE("LTC6804 adapter init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    BMS_LOGI("LTC6804 hardware BMS adapter initialized");
    return ESP_OK;
}

/// This function reads one BMS sample from the LTC6804 hardware. It reads cell voltages
/// from the LTC6804 ADC module, sums them for pack voltage, and handles pack current
/// based on the current_enable configuration flag.
///
/// \param out Pointer to output sample structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t ltc6804_adapter_read_sample(bms_sample_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read cell voltages from LTC6804 (includes internal retries)
    esp_err_t ret = ltc6804_read_cell_voltages(out->cell_v, g_cfg.battery.num_cells);
    if (ret != ESP_OK) {
        return ret;
    }

    // Compute pack voltage as sum of cell voltages
    float pack_v = 0.0f;
    for (int i = 0; i < g_cfg.battery.num_cells; ++i) {
        pack_v += out->cell_v[i];
    }
    out->pack_v = pack_v;

    // Read pack current from current sensor connected to LTC6804 GPIO pin
    if (g_cfg.battery.current_enable) {
        ret = ltc6804_read_current(&out->pack_i);
        if (ret != ESP_OK) {
            out->pack_i = 0.0f;
        }
    } else {
        out->pack_i = 0.0f;
    }

    // Set timestamp
    out->timestamp = xTaskGetTickCount();

    return ESP_OK;
}
