cmake_minimum_required(VERSION 3.16)

option(NES_SDR_INTEGRATION "Build the bench-gated NES-SDR RF integration" OFF)
if(NES_SDR_INTEGRATION)
    if(NOT DEFINED ENV{NES_SDR_SOURCE_ROOT} OR NOT DEFINED ENV{ESP_SDR_SOURCE_ROOT})
        message(FATAL_ERROR "Set NES_SDR_SOURCE_ROOT and ESP_SDR_SOURCE_ROOT for the live build")
    endif()
    file(TO_CMAKE_PATH "$ENV{NES_SDR_SOURCE_ROOT}" NES_SDR_SOURCE_ROOT)
    file(TO_CMAKE_PATH "$ENV{ESP_SDR_SOURCE_ROOT}" ESP_SDR_SOURCE_ROOT)
    list(APPEND EXTRA_COMPONENT_DIRS
        "${ESP_SDR_SOURCE_ROOT}/components/esp-dsp"
        "${CMAKE_CURRENT_LIST_DIR}/integration_components/nes_sdr"
        "${CMAKE_CURRENT_LIST_DIR}/integration_components/esp_sdr")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rom_vomitter_fc)
