set(esp_sdr_root "${ESP_SDR_SOURCE_ROOT}")
set(esp_sdr_main "${esp_sdr_root}/main")
idf_component_register(
    SRCS
        "${esp_sdr_main}/targets/esp32s3/receiver.c"
        "${esp_sdr_main}/common/burst_serial.c"
        "${esp_sdr_main}/common/ring_capture.c"
        "${esp_sdr_main}/common/spectrum_stats.c"
        "${esp_sdr_main}/targets/esp32s3/s3_fft_rnd.S"
        "${esp_sdr_main}/targets/esp32s3/s3_unpack.S"
        "${esp_sdr_main}/targets/esp32s3/s3_core1.S"
        "${esp_sdr_main}/targets/esp32s3/s3_firdot.S"
    INCLUDE_DIRS
        "${esp_sdr_main}/targets/esp32s3"
    PRIV_INCLUDE_DIRS
        "${esp_sdr_main}/common"
        "${esp_sdr_main}/diagnostics"
    LDFRAGMENTS "linker.lf"
    REQUIRES esp_phy esp_wifi nvs_flash esp_timer esp_driver_uart
             esp_driver_usb_serial_jtag esp-dsp
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP_SDR_EMBEDDED=1)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-T${esp_sdr_main}/targets/esp32s3/sram_guard.ld")
