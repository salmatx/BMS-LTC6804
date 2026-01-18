idf_component_register(
    SRCS
        "main.c"
        "appsm.c"
        "initialization.c"
        "tasksFC.c"
        "tasksSC.c"
        "spiffs.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        common
        bms
        process
        network
        http
        spiffs
        nvs_flash
)

spiffs_create_partition_image(spiffs ../spiffs FLASH_IN_PROJECT)