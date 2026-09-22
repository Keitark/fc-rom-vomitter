#include "usb_loader.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"
#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ines.h"
#include "rom_transport_protocol.h"
#include "sdkconfig.h"

static const char *TAG = "usb_loader";

#if CONFIG_NESCART_USB_UPLOAD_ENABLE

bool usb_loader_enabled(void)
{
    return true;
}

static int read_exact(void *output, size_t length, TickType_t timeout)
{
    uint8_t *bytes = output;
    size_t received = 0;
    const TickType_t started = xTaskGetTickCount();
    while (received < length) {
        const TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) {
            return -1;
        }
        const int count = usb_serial_jtag_read_bytes(
            bytes + received, length - received, pdMS_TO_TICKS(250));
        if (count > 0) {
            received += (size_t)count;
        }
    }
    return 0;
}

static int find_magic(uint8_t header[ROM_USB_HEADER_SIZE])
{
    static const uint8_t magic[4] = {'R', 'V', 'U', 'P'};
    size_t matched = 0;
    while (matched < sizeof(magic)) {
        uint8_t value = 0;
        if (usb_serial_jtag_read_bytes(&value, 1, portMAX_DELAY) != 1) {
            continue;
        }
        if (value == magic[matched]) {
            header[matched++] = value;
        } else {
            matched = value == magic[0] ? 1u : 0u;
            if (matched == 1u) {
                header[0] = value;
            }
        }
    }
    return 0;
}

static void send_response(const char *format, ...)
{
    char response[224];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(response, sizeof(response), format, args);
    va_end(args);
    if (length <= 0) {
        return;
    }
    const size_t bounded = (size_t)length < sizeof(response)
                               ? (size_t)length
                               : sizeof(response) - 1u;
    (void)usb_serial_jtag_write_bytes(response, bounded, pdMS_TO_TICKS(1000));
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
}

static void usb_loader_task(void *context)
{
    (void)context;
    uint8_t header_bytes[ROM_USB_HEADER_SIZE];
    for (;;) {
        (void)find_magic(header_bytes);
        if (read_exact(header_bytes + 4, ROM_USB_HEADER_SIZE - 4,
                       pdMS_TO_TICKS(CONFIG_NESCART_USB_UPLOAD_TIMEOUT_MS)) != 0) {
            send_response("RVER timeout incomplete_header\n");
            continue;
        }

        rom_usb_header_t header;
        char error[160];
        if (rom_usb_header_decode(header_bytes, sizeof(header_bytes),
                                  NESCART_MAX_INES_SIZE, &header,
                                  error, sizeof(error)) != 0) {
            send_response("RVER header %s\n", error);
            continue;
        }

        uint8_t *payload = malloc(header.payload_length);
        if (payload == NULL) {
            send_response("RVER memory allocation_failed\n");
            continue;
        }
        if (read_exact(payload, header.payload_length,
                       pdMS_TO_TICKS(CONFIG_NESCART_USB_UPLOAD_TIMEOUT_MS)) != 0) {
            free(payload);
            send_response("RVER timeout incomplete_payload\n");
            continue;
        }
        const uint32_t actual_crc = nescart_crc32(payload, header.payload_length);
        if (actual_crc != header.payload_crc32) {
            free(payload);
            send_response("RVER crc expected_%08lx_got_%08lx\n",
                          (unsigned long)header.payload_crc32,
                          (unsigned long)actual_crc);
            continue;
        }

        const esp_err_t err = controller_install_ines(
            payload, header.payload_length, error, sizeof(error));
        free(payload);
        if (err != ESP_OK) {
            send_response("RVER install %s\n", error);
            continue;
        }
        controller_status_t status;
        controller_get_status(&status);
        send_response("RVOK sequence=%lu image_crc32=%08lx\n",
                      (unsigned long)status.sequence,
                      (unsigned long)status.image_crc32);
    }
}

esp_err_t usb_loader_start(void)
{
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 4096,
    };
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&config), TAG,
                        "USB Serial/JTAG driver install failed");
    if (xTaskCreate(usb_loader_task, "usb_rom", 8192, NULL, 6, NULL) != pdPASS) {
        (void)usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "USB-C ROM upload protocol ready");
    return ESP_OK;
}

#else

bool usb_loader_enabled(void)
{
    return false;
}

esp_err_t usb_loader_start(void)
{
    return ESP_OK;
}

#endif
