idf_component_register(
    SRCS
        "app_main.c"
        "tabforge_ble.c"
        "layers/tabforge_layer1_device.c"
        "layers/tabforge_layer2_shell.c"
        "layers/tabforge_layer3_apps.c"
    INCLUDE_DIRS
        "."
        "layers"
    REQUIRES
        app_update
        bt
        esp_http_client
        esp_http_server
        esp-tls
        esp_wifi
        driver
        esp_driver_rmt
        esp_io_expander
        esp_event
        esp_netif
        esp_timer
        fatfs
        heap
        esp_hosted
        json
        mbedtls
        m5stack_tab5
        nvs_flash
        usb
        usb_host_cdc_acm
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TABFORGE_VERSION="${PROJECT_VER}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -include tabforge_forward.h
)
