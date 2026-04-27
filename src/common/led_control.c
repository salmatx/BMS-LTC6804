/// This module implements LED control functionality for ESP32 onboard LED.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "led_control.h"
#include "driver/gpio.h"
#include "logging.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/
/// Log module tag used by logging module
#define LOG_MODULE_TAG "LED_CTRL"
/// GPIO pin for onboard LED
#define LED_GPIO 2

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/
/// Current LED state (true = ON, false = OFF)
static bool s_led_state = false;

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes LED control GPIO.
///
/// \param None
/// \return ESP_OK on success, otherwise an error code
esp_err_t led_control_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        BMS_LOGE("LED GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    gpio_set_level(LED_GPIO, 0);
    s_led_state = false;
    BMS_LOGI("LED control initialized (GPIO %d)", LED_GPIO);
    
    return ESP_OK;
}

/// This function turns the LED on.
///
/// \param None
/// \return None
void led_control_turn_on(void)
{
    gpio_set_level(LED_GPIO, 1);
    s_led_state = true;
    BMS_LOGI("LED turned ON");

    return;
}

/// This function turns the LED off.
///
/// \param None
/// \return None
void led_control_turn_off(void)
{
    gpio_set_level(LED_GPIO, 0);
    s_led_state = false;
    BMS_LOGI("LED turned OFF");

    return;
}

/// This function gets current state of LED.
///
/// \param None
/// \return true if LED is ON, false if OFF
bool led_control_get_state(void)
{
    return s_led_state;
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
