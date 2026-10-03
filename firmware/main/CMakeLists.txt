idf_component_register(
    SRCS
        "app_main.c"
        "ap_compat.c"
        "ap_dhcp_trace.c"
        "cloud_sync.c"
        "cloud_queue.c"
        "console_power.c"
        "controller.c"
        "ines.c"
        "nes_sdr_platform.c"
        "rom_slot.c"
        "rom_store.c"
        "rom_transport_protocol.c"
        "serial_status.c"
        "sram_bus.c"
        "status_led.c"
        "usb_loader.c"
        "web_server.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_adc
        esp_event
        esp_http_client
        esp_http_server
        esp_driver_usb_serial_jtag
        esp_netif
        esp_partition
        esp_timer
        esp_wifi
        json
        mbedtls
        nvs_flash
        nes_sdr
)

# Use the SDK's supported extension header, retaining its default lwIP hooks.
idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
target_compile_definitions(${lwip_lib} PRIVATE
    ESP_IDF_LWIP_HOOK_FILENAME="${CMAKE_CURRENT_SOURCE_DIR}/ap_dhcp_hooks.h")
