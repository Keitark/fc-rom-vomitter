#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"
#include "esp_timer.h"
#include "ines.h"
#include "web_upload.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

enum {
    VALID_16, VALID_32, VALID_TRAINER, EMPTY, OVERSIZED, MALFORMED,
    DISCONNECT, TIMEOUTS, FLASH_ERROR, DEADLINE_ACROSS_READS,
    DEADLINE_TRICKLE, TIMEOUT_RECOVERY,
};
static const char *const scenarios[] = {
    "valid_16", "valid_32", "valid_trainer", "empty", "oversized", "malformed",
    "disconnect", "timeouts", "controller_error", "deadline_across_reads",
    "deadline_trickle", "timeout_recovery",
};
static unsigned scenario;
static uint8_t rom[NESCART_MAX_INES_SIZE];
static size_t rom_length;
static size_t read_offset;
static unsigned recv_calls;
static unsigned controller_calls;
static unsigned response_calls;
static unsigned timeout_results;
static bool normalized;
static bool connection_closed;
static httpd_req_t request;
static const char *response_status = "200 OK";
static const char *response_type;
static char response_body[192];
static int64_t now_us = 1000000;
static nescart_image_t reference;
static struct {
    uint8_t before[16];
    nescart_image_t image;
    uint8_t after[16];
} installed;

/* Only this target's real upload module redirects libc allocation calls. */
void *web_upload_test_malloc(size_t size)
{ CHECK(size <= 1024); return malloc(size); }
void *web_upload_test_calloc(size_t count, size_t size)
{ CHECK(size == 0 || count <= 1024 / size); return calloc(count, size); }
void *web_upload_test_realloc(void *pointer, size_t size)
{ CHECK(size <= 1024); return realloc(pointer, size); }
void web_upload_test_free(void *pointer) { free(pointer); }

static void make_rom(unsigned prg_banks, bool trainer)
{
    const size_t trainer_length = trainer ? 512 : 0;
    rom_length = 16 + trainer_length + prg_banks * 16384u + NESCART_CHR_SIZE;
    memset(rom, 0, sizeof(rom));
    memcpy(rom, "NES\x1a", 4);
    rom[4] = (uint8_t)prg_banks;
    rom[5] = 1;
    rom[6] = trainer ? 0x04 : 0x01;
    for (size_t i = 16; i < rom_length; ++i) rom[i] = (uint8_t)(i * 17u + 3u);
    char error[128];
    CHECK(ines_normalize(rom, rom_length, &reference, error, sizeof(error)) == 0);
    memset(&installed, 0xa5, sizeof(installed));
    request.content_len = rom_length;
}

int64_t esp_timer_get_time(void) { return now_us; }

int httpd_req_recv(httpd_req_t *req, char *buffer, size_t length)
{
    CHECK(req == &request && buffer != NULL && length > 0 && length <= 1024);
    ++recv_calls;
    if (scenario == TIMEOUTS ||
        (scenario == TIMEOUT_RECOVERY && (recv_calls == 1 || recv_calls == 2 ||
                                         recv_calls == 4 || recv_calls == 5))) {
        ++timeout_results;
        now_us += 1000000;
        return HTTPD_SOCK_ERR_TIMEOUT;
    }
    const size_t end = scenario == DISCONNECT ? 16 + 4096u : rom_length;
    if (read_offset >= end) return 0;
    size_t count = end - read_offset;
    if (count > length) count = length;
    const size_t fragment = scenario == DEADLINE_TRICKLE ? 1 : 137;
    if (count > fragment) count = fragment;
    memcpy(buffer, rom + read_offset, count);
    read_offset += count;
    if (scenario == DEADLINE_ACROSS_READS && recv_calls == 1) now_us += 30000001;
    if (scenario == DEADLINE_TRICKLE) now_us += 1000000;
    return (int)count;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status)
{ CHECK(req == &request && status != NULL); response_status = status; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{ CHECK(req == &request && type != NULL); response_type = type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    CHECK(req == &request && field != NULL && value != NULL);
    if (strcmp(field, "Connection") == 0) {
        CHECK(strcmp(value, "close") == 0);
        connection_closed = true;
    }
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body)
{
    CHECK(req == &request && body != NULL && strlen(body) < sizeof(response_body));
    ++response_calls;
    memcpy(response_body, body, strlen(body) + 1);
    return ESP_OK; /* Sending an error page succeeds; handler must still fail. */
}
esp_err_t httpd_resp_send_custom_err(httpd_req_t *req, const char *status,
                                    const char *message)
{
    CHECK(httpd_resp_set_status(req, status) == ESP_OK);
    CHECK(httpd_resp_set_type(req, "text/plain") == ESP_OK);
    return httpd_resp_sendstr(req, message);
}

esp_err_t controller_install_ines_stream(ines_read_exact_fn read_exact,
                                         void *context, size_t length,
                                         char *error, size_t error_length)
{
    CHECK(read_exact != NULL && context != NULL && length == request.content_len);
    ++controller_calls;
    if (ines_normalize_stream(read_exact, context, length, &installed.image,
                               error, error_length) != 0) return ESP_ERR_INVALID_ARG;
    normalized = true;
    if (scenario == FLASH_ERROR) {
        CHECK(error_length > strlen("flash write failed"));
        memcpy(error, "flash write failed", sizeof("flash write failed"));
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void check_guards(void)
{
    for (size_t i = 0; i < sizeof(installed.before); ++i) {
        CHECK(installed.before[i] == 0xa5 && installed.after[i] == 0xa5);
    }
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    for (scenario = 0; scenario < sizeof(scenarios) / sizeof(scenarios[0]); ++scenario) {
        if (strcmp(argv[1], scenarios[scenario]) == 0) break;
    }
    CHECK(scenario < sizeof(scenarios) / sizeof(scenarios[0]));
    make_rom(scenario == VALID_32 || scenario == VALID_TRAINER ? 2 : 1,
             scenario == VALID_TRAINER);
    if (scenario == EMPTY) request.content_len = 0;
    if (scenario == OVERSIZED) request.content_len = NESCART_MAX_INES_SIZE + 1u;
    if (scenario == MALFORMED) rom[0] = 'X';
    const esp_err_t result = web_upload_handler(&request);
    CHECK(response_calls == 1 && response_body[0] != '\0');
    check_guards();
    if (scenario <= VALID_TRAINER || scenario == TIMEOUT_RECOVERY) {
        CHECK(result == ESP_OK && strcmp(response_status, "200 OK") == 0);
        CHECK(response_type != NULL && strcmp(response_type, "application/json") == 0);
        CHECK(strstr(response_body, "\"ok\":true") != NULL);
        CHECK(!connection_closed && normalized && controller_calls == 1);
        CHECK(read_offset == rom_length && nescart_image_equal(&reference, &installed.image));
        if (scenario == TIMEOUT_RECOVERY) CHECK(timeout_results == 4);
    } else {
        const char *expected = scenario == EMPTY || scenario == OVERSIZED
                                   ? "413 Payload Too Large"
                                   : scenario == MALFORMED ? "400 Bad Request"
                                   : scenario == FLASH_ERROR ? "500 Internal Server Error"
                                   : "408 Request Timeout";
        CHECK(strcmp(response_status, expected) == 0);
        CHECK(result != ESP_OK); /* ESP_OK makes HTTPD purge the unread request body. */
        CHECK(connection_closed);
        if (scenario == EMPTY || scenario == OVERSIZED) {
            CHECK(controller_calls == 0 && recv_calls == 0 && read_offset == 0);
        } else {
            CHECK(controller_calls == 1);
        }
        if (scenario == MALFORMED) CHECK(read_offset == 16 && read_offset < rom_length);
        if (scenario == DISCONNECT) CHECK(read_offset == 16 + 4096u && !normalized);
        if (scenario == TIMEOUTS) CHECK(recv_calls == 3 && timeout_results == 3 && read_offset == 0);
        if (scenario == FLASH_ERROR) {
            CHECK(normalized && read_offset == rom_length);
            CHECK(strcmp(response_body, "flash write failed") == 0);
        }
        if (scenario == DEADLINE_ACROSS_READS) CHECK(recv_calls == 1 && read_offset == 16);
        if (scenario == DEADLINE_TRICKLE) CHECK(recv_calls == 30 && read_offset == 30);
        CHECK(scenario == FLASH_ERROR || !normalized);
    }
    printf("Web upload %s: response, streaming bounds and session outcome pass\n", argv[1]);
    return 0;
}
