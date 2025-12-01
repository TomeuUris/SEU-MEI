set(srcs "5.c")
idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES console esp_timer driver)

# Agregar definiciones del compilador para habilitar hooks de FreeRTOS
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    configUSE_TRACE_FACILITY=1
    configGENERATE_RUN_TIME_STATS=1
    configUSE_STATS_FORMATTING_FUNCTIONS=1
)
