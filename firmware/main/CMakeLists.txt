set(nescart_sources
    "app_main.c" "cloud_sync.c" "cloud_queue.c" "console_power.c"
    "controller.c" "ines.c" "rom_slot.c" "rom_store.c"
    "rom_transport_protocol.c" "sram_bus.c" "status_led.c"
    "usb_loader.c" "web_server.c")
set(nescart_dependencies
    esp_adc esp_event esp_http_client esp_http_server esp_driver_gpio
    esp_driver_usb_serial_jtag esp_netif esp_partition esp_wifi
    json mbedtls nvs_flash)
if(NES_SDR_INTEGRATION)
    list(APPEND nescart_dependencies nes_sdr esp_sdr)
endif()
if(CONFIG_NESCART_NES_SDR_LIVE_ENABLE)
    list(APPEND nescart_sources "nes_sdr_task.c")
endif()

idf_component_register(
    SRCS
        ${nescart_sources}
    INCLUDE_DIRS "."
    REQUIRES
        ${nescart_dependencies}
)
