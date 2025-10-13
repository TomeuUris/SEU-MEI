idf_component_register(SRCS "mcpwm_sync_example_main.c"
                       PRIV_REQUIRES esp_driver_mcpwm esp_driver_gpio esp_driver_ledc
                       INCLUDE_DIRS ".")
