/// ESP32 internal ADC oneshot driver module.
/// Configures ADC Unit 1 in oneshot mode for reading analog input pins (GPIO 34, 35, 36, 39).

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "adc.h"
#include "logging.h"
#include "esp_adc/adc_oneshot.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag
#define LOG_MODULE_TAG "ADC"

/// Number of configured ADC pins
#define ADC_NUM_PINS  4

/// Invalid ADC channel sentinel (no ADC_CHANNEL_MAX in ESP-IDF enum)
#define ADC_CHANNEL_INVALID  ((adc_channel_t) -1)

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static adc_channel_t pin_to_channel(adc_pin_t pin);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/
/// List of all configured ADC pins for bulk initialization
static const adc_pin_t s_adc_pins[ADC_NUM_PINS] = {
    ADC_PIN_GPIO34,
    ADC_PIN_GPIO35,
    ADC_PIN_GPIO36,
    ADC_PIN_GPIO39,
};

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// ADC oneshot unit handle
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes ADC Unit 1 in oneshot mode and configures all analog input channels
/// with 12-bit resolution and 12 dB attenuation (0–3.3 V range).
///
/// \return ESP_OK on success, otherwise an error code
esp_err_t adc_init(void)
{
    // Initialize ADC Unit 1
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        BMS_LOGE("ADC unit init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Channel configuration: 12-bit width, 12 dB attenuation
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    // Configure all pins
    for (int i = 0; i < ADC_NUM_PINS; ++i) {
        adc_channel_t ch = pin_to_channel(s_adc_pins[i]);
        ret = adc_oneshot_config_channel(s_adc_handle, ch, &chan_cfg);
        if (ret != ESP_OK) {
            BMS_LOGE("ADC config channel for GPIO %d failed: %s",
                     (int)s_adc_pins[i], esp_err_to_name(ret));
            adc_oneshot_del_unit(s_adc_handle);
            s_adc_handle = NULL;
            return ret;
        }
    }

    BMS_LOGI("ADC Unit 1 initialized");
    return ESP_OK;
}

/// This function performs a single ADC conversion on the specified analog pin.
///
/// \param pin        GPIO pin to read (must be one of adc_pin_t values)
/// \param raw_value  Pointer to receive the raw ADC value (0–4095)
/// \return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized,
///         ESP_ERR_INVALID_ARG on bad pin or NULL pointer
esp_err_t adc_read(adc_pin_t pin, int *raw_value)
{
    if (!raw_value) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_adc_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    adc_channel_t ch = pin_to_channel(pin);
    if (ch == ADC_CHANNEL_INVALID) {
        return ESP_ERR_INVALID_ARG;
    }

    return adc_oneshot_read(s_adc_handle, ch, raw_value);
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// This function maps an adc_pin_t GPIO number to the corresponding ADC1 channel.
///
/// \param pin  GPIO pin number
/// \return ADC channel, or ADC_CHANNEL_INVALID if the pin is not valid
static adc_channel_t pin_to_channel(adc_pin_t pin)
{
    switch (pin) {
        case ADC_PIN_GPIO36: return ADC_CHANNEL_0;  // GPIO 36 = ADC1_CH0
        case ADC_PIN_GPIO39: return ADC_CHANNEL_3;  // GPIO 39 = ADC1_CH3
        case ADC_PIN_GPIO34: return ADC_CHANNEL_6;  // GPIO 34 = ADC1_CH6
        case ADC_PIN_GPIO35: return ADC_CHANNEL_7;  // GPIO 35 = ADC1_CH7
        default:             return ADC_CHANNEL_INVALID;
    }
}
