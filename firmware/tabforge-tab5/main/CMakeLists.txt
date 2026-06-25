idf_component_register(
    SRCS
        "app_main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        app_update
        esp_event
        esp_netif
        nvs_flash
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TABFORGE_VERSION="${PROJECT_VER}"
)
