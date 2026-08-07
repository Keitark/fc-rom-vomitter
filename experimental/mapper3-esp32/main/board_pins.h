#pragma once

#include "driver/gpio.h"

/*
 * Released hardware-fc U23 mapping. Keep synchronized with
 * hardware-fc/tools/gen_sch.py ESP32_MAP.
 */
enum {
    PIN_MCU_D0 = GPIO_NUM_4,
    PIN_MCU_D1 = GPIO_NUM_5,
    PIN_MCU_D2 = GPIO_NUM_6,
    PIN_MCU_D3 = GPIO_NUM_7,
    PIN_MCU_D4 = GPIO_NUM_15,
    PIN_MCU_D5 = GPIO_NUM_16,
    PIN_MCU_D6 = GPIO_NUM_17,
    PIN_MCU_D7 = GPIO_NUM_18,

    PIN_SR_SER = GPIO_NUM_8,
    PIN_SR_SRCLK = GPIO_NUM_9,
    PIN_SR_RCLK = GPIO_NUM_10,

    PIN_PRG_WE_N = GPIO_NUM_11,
    PIN_CHR_WE_N = GPIO_NUM_12,
    PIN_PRG_OE_N = GPIO_NUM_13,
    PIN_CHR_OE_N = GPIO_NUM_14,
    PIN_LOAD_MODE = GPIO_NUM_21,
    PIN_PRG_MCU_EN_N = GPIO_NUM_47,
    PIN_CHR_MCU_EN_N = GPIO_NUM_48,
    PIN_MCU_DATA_DIR = GPIO_NUM_35,
    /* Experimental direct bodge output to isolated U14 A13. */
    PIN_CHR_BANK_LOAD = GPIO_NUM_36,
    PIN_MIRROR_SEL = GPIO_NUM_37,
    PIN_NES5V_SENSE = GPIO_NUM_1,
    PIN_LED_STATUS = GPIO_NUM_2,
    PIN_BOOT_BUTTON = GPIO_NUM_0,

    PIN_USB_DN = GPIO_NUM_19,
    PIN_USB_DP = GPIO_NUM_20,
    /*
     * Runtime Mapper-3 inputs. GPIO43/44 reuse J3 UART pins 2/3, and GPIO42
     * is U23 module pad 35. UART output is unavailable while RUN polling is
     * active.
     */
    PIN_MAPPER_CPU_D0 = GPIO_NUM_42,
    PIN_MAPPER_ROMSEL_INV = GPIO_NUM_43,
    PIN_MAPPER_PRG_EN_N = GPIO_NUM_44,
    PIN_DBG_RX = PIN_MAPPER_PRG_EN_N,
    PIN_DBG_TX = PIN_MAPPER_ROMSEL_INV,
};
