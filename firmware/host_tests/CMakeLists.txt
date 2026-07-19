cmake_minimum_required(VERSION 3.16)
project(nescart_host_tests C)

enable_testing()
add_executable(test_firmware_core
    test_firmware_core.c
    ../main/ines.c
    ../main/rom_slot.c
)
target_include_directories(test_firmware_core PRIVATE ../main)
if(MSVC)
    target_compile_options(test_firmware_core PRIVATE /W4 /WX)
else()
    target_compile_options(test_firmware_core PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME firmware_core COMMAND test_firmware_core)
