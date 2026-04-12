/// Module providing adapter interface for getting BMS data samples.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "bms_adapter.h"
#include "ltc6804.h"
#include "configuration.h"
#include "logging.h"
#include "adc.h"
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
/// PT1000 reference resistance at 0 deg C (Ohms)
#define PT1000_R0      1000.0f
/// Callendar-Van Dusen coefficient A (IEC 60751)
#define PT1000_A       3.9083e-3f
/// Callendar-Van Dusen coefficient B (IEC 60751)
#define PT1000_B      -5.775e-7f
/// Callendar-Van Dusen coefficient C (IEC 60751, used below 0 deg C only)
#define PT1000_C      -4.183e-12f
/// ESP32 ADC reference voltage (V)
#define PT1000_V_REF   3.3f
/// Reference resistor in voltage divider circuit (Ohms)
#define PT1000_R_REF   1000.0f

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
static float read_current(adc_pin_t pin, float i_min, float i_max);
static float read_pt1000(adc_pin_t pin);
static float read_temperature(void);

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

/// This function reads one demo BMS sample. Generates random per-cell voltages in range
/// [cell_v_min * 0.8, cell_v_max * 1.2] (80%-120% of configured limits) and random pack current
/// in range [series_pack_i_min * 1.2, series_pack_i_max * 1.2] so values can occasionally exceed thresholds.
///
/// \param out Pointer to output sample structure
/// \return ESP_OK on success, otherwise an error code
static esp_err_t demo_read_sample(bms_sample_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    // Extend configured limits by 20% to allow threshold crossings
    const float v_lo = g_cfg.battery.cell_v_min * 0.8f;
    const float v_hi = g_cfg.battery.cell_v_max * 1.2f;

    float pack_v = 0.0f;

    for (int i = 0; i < g_cfg.battery.num_cells; ++i) {
        float v = v_lo + demo_rand01() * (v_hi - v_lo);
        out->cell_v[i] = v;
        pack_v += v;
    }

    out->pack_v = pack_v;

    // Generate random pack current (only if current measurement enabled)
    if (g_cfg.battery.current_enable) {
        float i_lo = g_cfg.battery.series_pack_i_min * 0.8f;
        float i_hi = g_cfg.battery.series_pack_i_max * 1.2f;
        out->pack_i = i_lo + demo_rand01() * (i_hi - i_lo);
    } else {
        out->pack_i = 0.0f;
    }

    // Generate random temperature (only if temperature measurement enabled)
    if (g_cfg.battery.temperature_enable) {
        out->temperature = demo_rand01() * 60.0f; // 0 to 60 deg C
    } else {
        out->temperature = 0.0f;
    }

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



    // Read pack current from current sensor connected to LTC6804
    out->pack_i = read_current(ADC_PIN_GPIO34, 0, 50); // 0-50A range corresponding to 50 amp LEM HTB 50-P/SP5 sensor

    // Read temperature from sensor
    out->temperature = read_temperature();

    // Set timestamp
    out->timestamp = xTaskGetTickCount();

    return ESP_OK;
}

/// This function reads the current from an ADC pin and converts it to a float value in amps using linear mapping.
/// The mapping assumes that a raw ADC value of 0 corresponds to i_min and a raw ADC value of ADC_RANGE corresponds to i_max.
///
/// \param pin ADC pin to read
/// \param i_min Minimum expected current corresponding to raw ADC value of 0
/// \param i_max Maximum expected current corresponding to raw ADC value of ADC_RANGE
/// \return Current in amps corresponding to the raw ADC reading, scaled to the [i_min, i_max] range. If ADC read fails, returns 0.
static float read_current(adc_pin_t pin, float i_min, float i_max)
{
    // Read raw ADC value
    int raw_value = 0;
    esp_err_t ret = adc_read(pin, &raw_value);
    if (ret != ESP_OK) {
        BMS_LOGE("ADC read failed for pin %d: %s", (int)pin, esp_err_to_name(ret));
        return 0;
    }

    // Convert raw ADC value to current using linear mapping
    // Assuming raw_value of 0 corresponds to i_min and raw_value of 4095 corresponds to i_max
    float current = i_min + (i_max - i_min) * (float)raw_value / (float)ADC_RANGE;
    return current;
}

/// This function reads the resistance of a PT1000 sensor connected via a voltage divider
/// to the specified ADC pin and converts it to temperature in degrees Celsius.
/// Uses the Callendar-Van Dusen equation (IEC 60751) to convert resistance to temperature.
///
/// \param pin ADC pin connected to the PT1000 voltage divider output
/// \return Temperature in degrees Celsius, or 0 if reading fails
static float read_pt1000(adc_pin_t pin)
{
    int raw_value = 0;
    esp_err_t ret = adc_read(pin, &raw_value);
    if (ret != ESP_OK) {
        BMS_LOGE("ADC read failed for pin %d: %s", (int)pin, esp_err_to_name(ret));
        return 0;
    }

    // Convert raw ADC value to voltage
    float voltage = (float)raw_value * PT1000_V_REF / (float)ADC_RANGE;

    // Calculate PT1000 resistance from voltage divider: R_pt1000 = R_ref * V / (V_ref - V)
    float denom = PT1000_V_REF - voltage;
    if (denom <= 0.0f) {
        BMS_LOGE("PT1000 voltage divider error: voltage too high");
        return 0;
    }
    float resistance = PT1000_R_REF * voltage / denom;

    // Convert resistance to temperature using Callendar-Van Dusen equation (IEC 60751)
    // For T >= 0: R(T) = R0*(1 + A*T + B*T^2)                 - solve quadratic
    // For T <  0: R(T) = R0*(1 + A*T + B*T^2 + C*(T-100)*T^3) - Newton-Raphson
    float ratio = resistance / PT1000_R0;
    float discriminant = PT1000_A * PT1000_A - 4.0f * PT1000_B * (1.0f - ratio);
    if (discriminant < 0.0f) {
        BMS_LOGE("PT1000 conversion error: resistance out of range");
        return 0;
    }
    // Only positive root is valid. Check by assuming ratio as 0 (0 deg C). Calculation will be simplified to T1 = (A-A) / (2*B)
    // and T2 = (-A-A) / (2*B). Since B is negative, T1 will be 0 deg C and T2 will be outside of PT1000 range.
    float temperature = (-PT1000_A + sqrtf(discriminant)) / (2.0f * PT1000_B);

    // If quadratic yields T < 0, refine with full equation including C coefficient
    // using Newton-Raphson to calculate formula: f(T) = R0*(1 + A*T + B*T^2 + C*(T-100)*T^3)
    if (temperature < 0.0f) {
        float t = temperature; // initial guess from quadratic
        // Perform Newton-Raphson iterations according to formula x_{n+1} = x_n - f(x_n) / f'(x_n). Max number of iterations is 20.
        for (int iter = 0; iter < 20; ++iter) {
            float t2 = t * t;
            float t3 = t2 * t;
            // f(T) = R0*(1 + A*T + B*T^2 + C*(T-100)*T^3)
            float f  = PT1000_R0 * (1.0f + PT1000_A * t + PT1000_B * t2
                       + PT1000_C * (t - 100.0f) * t3) - resistance;
            // f'(T) = R0*(A + 2*B*T + C*(4*T^3 - 300*T^2))
            float df = PT1000_R0 * (PT1000_A + 2.0f * PT1000_B * t
                       + PT1000_C * (4.0f * t3 - 300.0f * t2));
            // Accuracy threshold for Newton-Raphson. For thermal degree of freedom is standard engineering value 1e-6.
            // Prevents to division by zero and limits iterations when close enough to solution.
            if (fabsf(df) < 1e-6f) {
                break;
            }
            // f(x_n) / f'(x_n)
            float dt = f / df;
            // x_n - f(x_n) / f'(x_n)
            t -= dt;
            // Stop iterations if change of temperature is below 0.001 deg C.
            if (fabsf(dt) < 0.001f) {
                break;
            }
        }
        temperature = t;
    }

    return temperature;
}

/// This function reads the temperature from the PT1000 sensor connected to GPIO35.
/// Acts as an adapter to allow future switching to a different thermal sensor.
///
/// \param None
/// \return Temperature in degrees Celsius
static float read_temperature(void)
{
    return read_pt1000(ADC_PIN_GPIO35);
}
