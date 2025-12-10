/// This module implements logging initialization and configuration functions used for logging across the project.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "logging.h"

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/

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

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function initializes the logging system with default log levels for all modules. Lower log levels than
/// selected will be suppressed.
///
/// \param None
/// \return None
void bms_logging_init(void)
{
    // Set default global log level to INFO
    esp_log_level_set("*", ESP_LOG_INFO);
}

/// This function sets the global log level for all modules. Lower log levels than selected will be suppressed.
///
/// \param level Log level to set
/// \return None
void bms_logging_set_global_level(esp_log_level_t level)
{
    esp_log_level_set("*", level);
}

/// This function sets the log level for a specific module identified by its tag. It overrides the global log setting
/// provided by \ref bms_logging_init(void).
///
/// \param module_tag Module tag string
/// \param level Log level to set
/// \return None
void bms_logging_set_module_level(const char *module_tag, esp_log_level_t level)
{
    if (module_tag != NULL) {
        esp_log_level_set(module_tag, level);
    }
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/