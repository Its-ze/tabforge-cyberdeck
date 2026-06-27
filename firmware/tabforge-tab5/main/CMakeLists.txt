idf_component_register(
    SRCS
        "app_main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        app_update
        esp_http_client
        esp_https_ota
        esp-tls
        esp_wifi
        driver
        esp_io_expander
        esp_event
        esp_netif
        esp_timer
        fatfs
        heap
        json
        m5stack_tab5
        nvs_flash
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TABFORGE_VERSION="${PROJECT_VER}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -include tabforge_forward.h
)
