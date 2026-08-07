#include "sram_bus.h"

#include <inttypes.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mapper_runtime.h"

typedef enum { CHIP_PRG, CHIP_CHR } sram_chip_t;

static const char *TAG = "sram_bus";
static const gpio_num_t DATA_PINS[8] = {
    PIN_MCU_D0, PIN_MCU_D1, PIN_MCU_D2, PIN_MCU_D3,
    PIN_MCU_D4, PIN_MCU_D5, PIN_MCU_D6, PIN_MCU_D7,
};
static bool s_console_exposed;

static void set_data_input(void)
{
    for (unsigned bit = 0; bit < 8; ++bit) {
        gpio_set_direction(DATA_PINS[bit], GPIO_MODE_INPUT);
        gpio_set_pull_mode(DATA_PINS[bit], GPIO_FLOATING);
    }
}

static void set_data_output(uint8_t value)
{
    for (unsigned bit = 0; bit < 8; ++bit) {
        gpio_set_level(DATA_PINS[bit], (value >> bit) & 1u);
    }
    for (unsigned bit = 0; bit < 8; ++bit) {
        gpio_set_direction(DATA_PINS[bit], GPIO_MODE_OUTPUT);
    }
}

static uint8_t read_data(void)
{
    uint8_t value = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        value |= (uint8_t)(gpio_get_level(DATA_PINS[bit]) << bit);
    }
    return value;
}

static void set_chr_load_bank(unsigned bank)
{
    gpio_set_level(PIN_CHR_BANK_LOAD, bank & 1u);
}

static void set_address(uint16_t address)
{
    /* Send A15 first so U15 ends with A0..A7 and U16 with A8..A15. */
    for (int bit = 15; bit >= 0; --bit) {
        gpio_set_level(PIN_SR_SER, (address >> bit) & 1u);
        gpio_set_level(PIN_SR_SRCLK, 1);
        esp_rom_delay_us(1);
        gpio_set_level(PIN_SR_SRCLK, 0);
    }
    gpio_set_level(PIN_SR_RCLK, 1);
    esp_rom_delay_us(1);
    gpio_set_level(PIN_SR_RCLK, 0);
}

static gpio_num_t chip_enable_pin(sram_chip_t chip)
{
    return chip == CHIP_PRG ? PIN_PRG_MCU_EN_N : PIN_CHR_MCU_EN_N;
}

static gpio_num_t chip_oe_pin(sram_chip_t chip)
{
    return chip == CHIP_PRG ? PIN_PRG_OE_N : PIN_CHR_OE_N;
}

static gpio_num_t chip_we_pin(sram_chip_t chip)
{
    return chip == CHIP_PRG ? PIN_PRG_WE_N : PIN_CHR_WE_N;
}

static void disable_mcu_data_buffers(void)
{
    gpio_set_level(PIN_PRG_MCU_EN_N, 1);
    gpio_set_level(PIN_CHR_MCU_EN_N, 1);
}

void sram_bus_hold_isolated(void)
{
    disable_mcu_data_buffers();
    gpio_set_level(PIN_PRG_WE_N, 1);
    gpio_set_level(PIN_CHR_WE_N, 1);
    gpio_set_level(PIN_PRG_OE_N, 1);
    gpio_set_level(PIN_CHR_OE_N, 1);
    set_data_input();
    gpio_set_level(PIN_MCU_DATA_DIR, 0);
    /* Disable the console bus before taking GPIO36 back from the mapper core. */
    gpio_set_level(PIN_LOAD_MODE, 1);
    esp_err_t mapper_err = mapper_runtime_stop();
    if (mapper_err != ESP_OK) {
        ESP_LOGE(TAG, "mapper stop failed: %s", esp_err_to_name(mapper_err));
    }
    gpio_set_level(PIN_CHR_BANK_LOAD, 0);
    gpio_set_direction(PIN_CHR_BANK_LOAD, GPIO_MODE_OUTPUT);
    s_console_exposed = false;
}

esp_err_t sram_bus_init_safe(void)
{
    const gpio_num_t outputs[] = {
        PIN_SR_SER, PIN_SR_SRCLK, PIN_SR_RCLK,
        PIN_PRG_WE_N, PIN_CHR_WE_N, PIN_PRG_OE_N, PIN_CHR_OE_N,
        PIN_PRG_MCU_EN_N, PIN_CHR_MCU_EN_N, PIN_MCU_DATA_DIR,
        PIN_MIRROR_SEL, PIN_CHR_BANK_LOAD,
    };

    /* Preload safe values before enabling each output driver. */
    gpio_set_level(PIN_SR_SER, 0);
    gpio_set_level(PIN_SR_SRCLK, 0);
    gpio_set_level(PIN_SR_RCLK, 0);
    gpio_set_level(PIN_PRG_WE_N, 1);
    gpio_set_level(PIN_CHR_WE_N, 1);
    gpio_set_level(PIN_PRG_OE_N, 1);
    gpio_set_level(PIN_CHR_OE_N, 1);
    gpio_set_level(PIN_PRG_MCU_EN_N, 1);
    gpio_set_level(PIN_CHR_MCU_EN_N, 1);
    gpio_set_level(PIN_MCU_DATA_DIR, 0);
    gpio_set_level(PIN_MIRROR_SEL, 0);
    gpio_set_level(PIN_CHR_BANK_LOAD, 0);

    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        ESP_ERROR_CHECK(gpio_set_direction(outputs[i], GPIO_MODE_OUTPUT));
    }
    set_data_input();

    gpio_set_level(PIN_LOAD_MODE, 1);
    ESP_ERROR_CHECK(gpio_set_direction(PIN_LOAD_MODE, GPIO_MODE_OUTPUT));
    ESP_RETURN_ON_ERROR(mapper_runtime_init(), TAG,
                        "runtime mapper init failed");
    s_console_exposed = false;
    ESP_LOGI(TAG, "safe LOAD topology asserted");
    return ESP_OK;
}

static esp_err_t write_region(sram_chip_t chip, const uint8_t *data, size_t length)
{
    const gpio_num_t enable = chip_enable_pin(chip);
    const gpio_num_t we = chip_we_pin(chip);
    gpio_set_level(chip_oe_pin(chip), 1);
    gpio_set_level(PIN_MCU_DATA_DIR, 1); /* U11/U12 A (ESP) -> B (SRAM). */
    set_data_output(0);
    gpio_set_level(enable, 0);

    for (size_t address = 0; address < length; ++address) {
        set_address((uint16_t)address);
        set_data_output(data[address]);
        gpio_set_level(we, 0);
        esp_rom_delay_us(1);
        gpio_set_level(we, 1);
        if ((address & 0x3ffu) == 0x3ffu) {
            vTaskDelay(1);
        }
    }
    gpio_set_level(enable, 1);
    set_data_input();
    gpio_set_level(PIN_MCU_DATA_DIR, 0);
    return ESP_OK;
}

static esp_err_t verify_region(sram_chip_t chip, const uint8_t *expected,
                               size_t length, uint32_t *verified_crc)
{
    const gpio_num_t enable = chip_enable_pin(chip);
    const gpio_num_t oe = chip_oe_pin(chip);
    set_data_input();
    gpio_set_level(PIN_MCU_DATA_DIR, 0); /* SRAM B -> ESP A. */
    gpio_set_level(enable, 0);
    gpio_set_level(oe, 0);

    uint32_t crc = nescart_crc32_begin();
    for (size_t address = 0; address < length; ++address) {
        set_address((uint16_t)address);
        esp_rom_delay_us(1);
        const uint8_t actual = read_data();
        crc = nescart_crc32_update(crc, &actual, 1);
        if (actual != expected[address]) {
            ESP_LOGE(TAG, "%s verify mismatch at 0x%04x: expected %02x got %02x",
                     chip == CHIP_PRG ? "PRG" : "CHR", (unsigned)address,
                     expected[address], actual);
            gpio_set_level(oe, 1);
            gpio_set_level(enable, 1);
            return ESP_ERR_INVALID_CRC;
        }
        if ((address & 0x3ffu) == 0x3ffu) {
            vTaskDelay(1);
        }
    }
    gpio_set_level(oe, 1);
    gpio_set_level(enable, 1);
    *verified_crc = nescart_crc32_finish(crc);
    return ESP_OK;
}

static esp_err_t enter_console_run(nescart_mirroring_t mirroring,
                                   nescart_mapper_t mapper)
{
    disable_mcu_data_buffers();
    set_data_input();
    gpio_set_level(PIN_MCU_DATA_DIR, 0);
    gpio_set_level(PIN_PRG_WE_N, 1);
    gpio_set_level(PIN_CHR_WE_N, 1);
    gpio_set_level(PIN_MIRROR_SEL,
                   mirroring == NESCART_MIRROR_HORIZONTAL ? 1 : 0);
    gpio_set_level(PIN_PRG_OE_N, 0);
    gpio_set_level(PIN_CHR_OE_N, 0);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_CHR_BANK_LOAD, 0);
    gpio_set_direction(PIN_CHR_BANK_LOAD, GPIO_MODE_OUTPUT);
    if (mapper == NESCART_MAPPER_CNROM) {
        ESP_RETURN_ON_ERROR(mapper_runtime_start(), TAG,
                            "runtime mapper start failed");
    } else {
        ESP_RETURN_ON_ERROR(mapper_runtime_stop(), TAG,
                            "runtime mapper stop failed");
    }
    /*
     * The CNROM poller is already live but LOAD_MODE-gated before this edge,
     * avoiding an unobserved console-write window during the handoff.
     */
    gpio_set_level(PIN_LOAD_MODE, 0);
    s_console_exposed = true;
    ESP_LOGI(TAG, "RUN topology exposed; mapper=%u, press Famicom RESET",
             (unsigned)mapper);
    return ESP_OK;
}

esp_err_t sram_bus_load_and_verify(const nescart_image_t *image,
                                   bool should_expose)
{
    sram_bus_hold_isolated();
    set_chr_load_bank(0);
    esp_err_t err = write_region(CHIP_PRG, image->data, NESCART_PRG_SIZE);
    for (unsigned bank = 0; err == ESP_OK && bank < NESCART_CHR_BANKS_MAX;
         ++bank) {
        set_chr_load_bank(bank);
        err = write_region(
            CHIP_CHR,
            image->data + NESCART_PRG_SIZE + bank * NESCART_CHR_BANK_SIZE,
            NESCART_CHR_BANK_SIZE);
    }
    uint32_t prg_crc = 0;
    uint32_t chr_crc[NESCART_CHR_BANKS_MAX] = {0};
    if (err == ESP_OK) {
        set_chr_load_bank(0);
        err = verify_region(CHIP_PRG, image->data, NESCART_PRG_SIZE, &prg_crc);
    }
    for (unsigned bank = 0; err == ESP_OK && bank < NESCART_CHR_BANKS_MAX;
         ++bank) {
        set_chr_load_bank(bank);
        err = verify_region(
            CHIP_CHR,
            image->data + NESCART_PRG_SIZE + bank * NESCART_CHR_BANK_SIZE,
            NESCART_CHR_BANK_SIZE, &chr_crc[bank]);
    }
    if (err != ESP_OK) {
        sram_bus_hold_isolated();
        return err;
    }
    ESP_LOGI(TAG,
             "SRAM verified: PRG %08" PRIx32 ", CHR0 %08" PRIx32
             ", CHR1 %08" PRIx32,
             prg_crc, chr_crc[0], chr_crc[1]);
    if (should_expose) {
        err = enter_console_run(image->mirroring, image->mapper);
        if (err != ESP_OK) {
            sram_bus_hold_isolated();
            return err;
        }
    } else {
        sram_bus_hold_isolated();
    }
    return ESP_OK;
}

bool sram_bus_console_exposed(void)
{
    return s_console_exposed;
}
