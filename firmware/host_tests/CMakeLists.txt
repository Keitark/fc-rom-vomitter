cmake_minimum_required(VERSION 3.16)
project(nescart_host_tests C)

enable_testing()
add_executable(test_firmware_core
    test_firmware_core.c
    ../main/ines.c
    ../main/rom_transport_protocol.c
    ../main/rom_slot.c
)
target_include_directories(test_firmware_core PRIVATE ../main)
if(MSVC)
    target_compile_options(test_firmware_core PRIVATE /W4 /WX)
else()
    target_compile_options(test_firmware_core PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME firmware_core COMMAND test_firmware_core)

add_executable(test_rf_startup test_rf_startup.c ../main/nes_sdr_platform.c)
set_property(TARGET test_rf_startup PROPERTY C_STANDARD 11)
target_include_directories(test_rf_startup PRIVATE
    esp_stubs ../main ../components/nes_sdr/include)
if(MSVC)
    target_compile_options(test_rf_startup PRIVATE /W4 /WX /experimental:c11atomics)
else()
    target_compile_options(test_rf_startup PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME rf_startup COMMAND test_rf_startup)

add_executable(test_serial_status test_serial_status.c ../main/serial_status.c)
target_include_directories(test_serial_status PRIVATE esp_stubs ../main)
if(MSVC)
    target_compile_options(test_serial_status PRIVATE /W4 /WX)
else()
    target_compile_options(test_serial_status PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME serial_status COMMAND test_serial_status)

add_executable(test_ap_compat test_ap_compat.c ../main/ap_compat.c)
target_include_directories(test_ap_compat PRIVATE esp_stubs ../main)
if(MSVC)
    target_compile_options(test_ap_compat PRIVATE /W4 /WX)
else()
    target_compile_options(test_ap_compat PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME ap_compat COMMAND test_ap_compat)

add_executable(test_ap_dhcp_trace test_ap_dhcp_trace.c ../main/ap_dhcp_trace.c)
set_property(TARGET test_ap_dhcp_trace PROPERTY C_STANDARD 11)
target_include_directories(test_ap_dhcp_trace PRIVATE esp_stubs ../main)
if(MSVC)
    target_compile_options(test_ap_dhcp_trace PRIVATE /W4 /WX /experimental:c11atomics)
else()
    target_compile_options(test_ap_dhcp_trace PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME ap_dhcp_trace COMMAND test_ap_dhcp_trace)
