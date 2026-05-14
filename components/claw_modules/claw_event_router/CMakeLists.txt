idf_component_register(
    SRCS
        "src/claw_event.c"
        "src/claw_event_router.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        claw_cap
        claw_core
        freertos
        json
        console
)

# Suppress -Wint-in-bool-context triggered by ESP_LOG_LEVEL macro in IDF 5.3.x
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-int-in-bool-context)
