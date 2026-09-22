#include "cloud_queue.h"

#if CONFIG_NESCART_CLOUD_PULL_ENABLE && CONFIG_NESCART_CLOUD_QUEUE_ENABLE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "controller.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "ines.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"

#ifndef CONFIG_NESCART_CLOUD_ALLOW_CONSOLE_RELOAD
#define CONFIG_NESCART_CLOUD_ALLOW_CONSOLE_RELOAD 0
#endif

static const char *TAG = "cloud_queue";
static const char *NEXT_PATH = "/api/device/v2/next";

typedef struct {
    char id[37];
    char sha256[65];
    char crc32[9];
    size_t bytes;
    int prg_kib;
    int chr_kib;
    char mirroring[12];
} queue_manifest_t;

static void to_hex(const uint8_t *bytes, size_t count, char *output)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
        output[2 * i] = digits[bytes[i] >> 4];
        output[2 * i + 1] = digits[bytes[i] & 15u];
    }
    output[2 * count] = '\0';
}

static bool hex_string(const char *value, size_t length)
{
    if (value == NULL || strlen(value) != length) return false;
    for (size_t i = 0; i < length; ++i) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}

static bool valid_job_id(const char *id)
{
    if (id == NULL || strlen(id) != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return false;
        } else if (!((id[i] >= '0' && id[i] <= '9') ||
                     (id[i] >= 'a' && id[i] <= 'f'))) return false;
    }
    return true;
}

static esp_err_t signed_request(const char *path, esp_http_client_method_t method,
                                const uint8_t *body, size_t body_length,
                                uint8_t *response, size_t capacity,
                                size_t *response_length, int *status_code)
{
    if (response == NULL || response_length == NULL || status_code == NULL ||
        (body_length > 0 && body == NULL)) return ESP_ERR_INVALID_ARG;

    const char *method_name = method == HTTP_METHOD_GET ? "GET" : "POST";
    const time_t now = time(NULL);
    if (now < 1700000000) return ESP_ERR_INVALID_STATE;

    uint8_t digest[32];
    if (mbedtls_sha256(body_length == 0 ? (const uint8_t *)"" : body,
                       body_length, digest, 0) != 0) return ESP_FAIL;
    char body_hash[65];
    to_hex(digest, sizeof(digest), body_hash);

    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    char nonce[33];
    to_hex(random_bytes, sizeof(random_bytes), nonce);
    char timestamp[20];
    snprintf(timestamp, sizeof(timestamp), "%lld", (long long)now);

    char canonical[300];
    const int canonical_length = snprintf(canonical, sizeof(canonical),
                                          "%s\n%s\n%s\n%s\n%s", method_name,
                                          path, timestamp, nonce, body_hash);
    if (canonical_length < 0 || (size_t)canonical_length >= sizeof(canonical))
        return ESP_ERR_INVALID_SIZE;
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (sha256 == NULL ||
        mbedtls_md_hmac(sha256,
                        (const uint8_t *)CONFIG_NESCART_CLOUD_DEVICE_HMAC_SECRET,
                        strlen(CONFIG_NESCART_CLOUD_DEVICE_HMAC_SECRET),
                        (const uint8_t *)canonical, (size_t)canonical_length,
                        digest) != 0) return ESP_FAIL;
    char signature[65];
    to_hex(digest, sizeof(digest), signature);

    char url[320];
    const int url_length = snprintf(url, sizeof(url), "%s%s",
                                    CONFIG_NESCART_CLOUD_SERVICE_ORIGIN, path);
    if (url_length < 0 || (size_t)url_length >= sizeof(url)) return ESP_ERR_INVALID_SIZE;
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = CONFIG_NESCART_CLOUD_HTTP_TIMEOUT_MS,
        .buffer_size = 1024,
        .user_agent = "fc-rom-vomitter/2 queue-client",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;

    char authorization[96];
    snprintf(authorization, sizeof(authorization), "RV-HMAC-SHA256 %s", signature);
    esp_err_t err = esp_http_client_set_header(client, "X-RV-Device", CONFIG_NESCART_CLOUD_DEVICE_ID);
    if (err == ESP_OK) err = esp_http_client_set_header(client, "X-RV-Timestamp", timestamp);
    if (err == ESP_OK) err = esp_http_client_set_header(client, "X-RV-Nonce", nonce);
    if (err == ESP_OK) err = esp_http_client_set_header(client, "X-RV-Body-SHA256", body_hash);
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Authorization", authorization);
    if (err == ESP_OK && method == HTTP_METHOD_POST)
        err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err != ESP_OK) goto cleanup;

    err = esp_http_client_open(client, body_length);
    if (err != ESP_OK) goto cleanup;
    for (size_t offset = 0; offset < body_length;) {
        const int written = esp_http_client_write(client, (const char *)body + offset,
                                                   body_length - offset);
        if (written <= 0) { err = ESP_FAIL; goto cleanup; }
        offset += (size_t)written;
    }
    const int64_t declared = esp_http_client_fetch_headers(client);
    *status_code = esp_http_client_get_status_code(client);
    if (declared > (int64_t)capacity) { err = ESP_ERR_INVALID_SIZE; goto cleanup; }
    *response_length = 0;
    if (*status_code == 204) goto cleanup;
    while (*response_length < capacity) {
        const int count = esp_http_client_read(client, (char *)response + *response_length,
                                                capacity - *response_length);
        if (count < 0) { err = ESP_FAIL; goto cleanup; }
        if (count == 0) break;
        *response_length += (size_t)count;
    }
    if (!esp_http_client_is_complete_data_received(client) ||
        (declared >= 0 && *response_length != (size_t)declared)) {
        err = ESP_ERR_INVALID_SIZE;
    }

cleanup:
    (void)esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static bool get_string(const cJSON *object, const char *key,
                       char *output, size_t capacity)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) return false;
    const size_t length = strlen(item->valuestring);
    if (length >= capacity) return false;
    memcpy(output, item->valuestring, length + 1);
    return true;
}

static bool parse_manifest(const uint8_t *json_bytes, size_t length,
                           queue_manifest_t *manifest)
{
    cJSON *root = cJSON_ParseWithLength((const char *)json_bytes, length);
    if (root == NULL) return false;
    char download_url[320];
    const cJSON *bytes = cJSON_GetObjectItemCaseSensitive(root, "bytes");
    const cJSON *ines = cJSON_GetObjectItemCaseSensitive(root, "ines");
    const cJSON *mapper = cJSON_GetObjectItemCaseSensitive(ines, "mapper");
    const cJSON *prg = cJSON_GetObjectItemCaseSensitive(ines, "prg_kib");
    const cJSON *chr = cJSON_GetObjectItemCaseSensitive(ines, "chr_kib");
    bool valid = get_string(root, "job_id", manifest->id, sizeof(manifest->id)) &&
        get_string(root, "sha256", manifest->sha256, sizeof(manifest->sha256)) &&
        get_string(root, "crc32", manifest->crc32, sizeof(manifest->crc32)) &&
        get_string(root, "download_url", download_url, sizeof(download_url)) &&
        get_string(ines, "mirroring", manifest->mirroring, sizeof(manifest->mirroring)) &&
        valid_job_id(manifest->id) && hex_string(manifest->sha256, 64) &&
        hex_string(manifest->crc32, 8) && cJSON_IsNumber(bytes) &&
        cJSON_IsNumber(mapper) && mapper->valueint == 0 &&
        cJSON_IsNumber(prg) && (prg->valueint == 16 || prg->valueint == 32) &&
        cJSON_IsNumber(chr) && chr->valueint == 8 &&
        (strcmp(manifest->mirroring, "horizontal") == 0 ||
         strcmp(manifest->mirroring, "vertical") == 0);
    if (valid) {
        manifest->bytes = (size_t)bytes->valueint;
        manifest->prg_kib = prg->valueint;
        manifest->chr_kib = chr->valueint;
        const size_t minimum = 16u + (size_t)manifest->prg_kib * 1024u + NESCART_CHR_SIZE;
        valid = manifest->bytes >= minimum && manifest->bytes <= NESCART_MAX_INES_SIZE &&
                (manifest->bytes == minimum || manifest->bytes == minimum + 512u);
    }
    char expected_url[320];
    if (valid) {
        const int count = snprintf(expected_url, sizeof(expected_url),
                                   "%s/api/device/v2/jobs/%s/rom",
                                   CONFIG_NESCART_CLOUD_SERVICE_ORIGIN, manifest->id);
        valid = count > 0 && (size_t)count < sizeof(expected_url) &&
                strcmp(download_url, expected_url) == 0;
    }
    cJSON_Delete(root);
    return valid;
}

static bool verify_payload(const uint8_t *bytes, size_t length,
                           const queue_manifest_t *manifest)
{
    if (length != manifest->bytes || length < 16 ||
        memcmp(bytes, "NES\x1a", 4) != 0 || bytes[5] != 1 ||
        bytes[4] != (uint8_t)(manifest->prg_kib / 16) ||
        (bytes[6] & 0x08u) != 0 ||
        ((bytes[6] >> 4) | (bytes[7] & 0xf0u)) != 0 ||
        ((bytes[7] & 0x0cu) == 0x08u) ||
        (((bytes[6] & 1u) != 0) != (strcmp(manifest->mirroring, "vertical") == 0)))
        return false;
    const size_t trainer = (bytes[6] & 4u) ? 512u : 0u;
    if (length != 16u + trainer + (size_t)manifest->prg_kib * 1024u + NESCART_CHR_SIZE)
        return false;
    uint8_t digest[32];
    if (mbedtls_sha256(bytes, length, digest, 0) != 0) return false;
    char sha256[65];
    to_hex(digest, sizeof(digest), sha256);
    char crc32[9];
    snprintf(crc32, sizeof(crc32), "%08lx", (unsigned long)nescart_crc32(bytes, length));
    return strcmp(sha256, manifest->sha256) == 0 &&
           strcmp(crc32, manifest->crc32) == 0;
}

static esp_err_t report_result(const queue_manifest_t *manifest,
                               const char *result, const char *code,
                               const char *message)
{
    char path[100];
    snprintf(path, sizeof(path), "/api/device/v2/jobs/%s/result", manifest->id);
    char body[300];
    const int length = snprintf(body, sizeof(body),
        "{\"result\":\"%s\",\"idempotency_key\":\"%s_%s\",\"code\":\"%s\",\"message\":\"%s\"}",
        result, manifest->id, result, code, message);
    if (length < 0 || (size_t)length >= sizeof(body)) return ESP_ERR_INVALID_SIZE;
    uint8_t response[512];
    size_t received = 0;
    int status = 0;
    esp_err_t err = signed_request(path, HTTP_METHOD_POST, (const uint8_t *)body,
                                    (size_t)length, response, sizeof(response),
                                    &received, &status);
    if (err == ESP_OK && status != 200) err = ESP_FAIL;
    return err;
}

esp_err_t cloud_queue_poll(cloud_queue_result_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    *result = CLOUD_QUEUE_WAITING;
    controller_status_t state;
    controller_get_status(&state);
    char capabilities[320];
    const int body_length = snprintf(capabilities, sizeof(capabilities),
        "{\"firmware\":\"2.0.0-dev\",\"protocol\":2,\"mappers\":[0],\"max_rom_bytes\":%u,"
        "\"console_power\":%s,\"console_exposed\":%s,\"can_interrupt_console\":%s}",
        (unsigned)NESCART_MAX_INES_SIZE, state.console_power ? "true" : "false",
        state.console_exposed ? "true" : "false",
        CONFIG_NESCART_CLOUD_ALLOW_CONSOLE_RELOAD ? "true" : "false");
    if (body_length < 0 || (size_t)body_length >= sizeof(capabilities))
        return ESP_ERR_INVALID_SIZE;

    uint8_t manifest_json[1536];
    size_t response_length = 0;
    int status = 0;
    esp_err_t err = signed_request(NEXT_PATH, HTTP_METHOD_POST,
                                    (const uint8_t *)capabilities, (size_t)body_length,
                                    manifest_json, sizeof(manifest_json),
                                    &response_length, &status);
    if (err != ESP_OK) return err;
    if (status == 204) return ESP_OK;
    if (status != 200) {
        ESP_LOGW(TAG, "queue claim returned HTTP %d", status);
        return ESP_FAIL;
    }

    queue_manifest_t manifest = {0};
    if (!parse_manifest(manifest_json, response_length, &manifest)) {
        ESP_LOGE(TAG, "invalid queue manifest");
        return ESP_ERR_INVALID_RESPONSE;
    }
    controller_get_status(&state);
    if ((state.console_power || state.console_exposed) &&
        !CONFIG_NESCART_CLOUD_ALLOW_CONSOLE_RELOAD) {
        *result = CLOUD_QUEUE_DEFERRED;
        return report_result(&manifest, "deferred", "console_unsafe",
                             "Console is powered; waiting for a safe changeover.");
    }

    uint8_t *payload = malloc(NESCART_MAX_INES_SIZE);
    if (payload == NULL) return ESP_ERR_NO_MEM;
    char path[100];
    snprintf(path, sizeof(path), "/api/device/v2/jobs/%s/rom", manifest.id);
    response_length = 0;
    err = signed_request(path, HTTP_METHOD_GET, NULL, 0, payload,
                         NESCART_MAX_INES_SIZE, &response_length, &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "ROM download failed (HTTP %d)", status);
        free(payload);
        return err == ESP_OK ? ESP_FAIL : err;
    }
    if (!verify_payload(payload, response_length, &manifest)) {
        free(payload);
        ESP_LOGE(TAG, "download did not match manifest; refusing install");
        (void)report_result(&manifest, "failed", "integrity_mismatch",
                            "Downloaded ROM failed independent verification.");
        return ESP_ERR_INVALID_CRC;
    }
    controller_get_status(&state);
    if ((state.console_power || state.console_exposed) &&
        !CONFIG_NESCART_CLOUD_ALLOW_CONSOLE_RELOAD) {
        free(payload);
        *result = CLOUD_QUEUE_DEFERRED;
        return report_result(&manifest, "deferred", "console_unsafe",
                             "Console became powered during download.");
    }
    const uint32_t before_sequence = state.sequence;
    char install_error[160] = {0};
    err = controller_install_ines(payload, response_length,
                                  install_error, sizeof(install_error));
    free(payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "queue install failed: %s", install_error);
        (void)report_result(&manifest, "failed", "install_failed",
                            "Cartridge could not verify the new ROM.");
        return err;
    }
    controller_get_status(&state);
    const bool unchanged = before_sequence == state.sequence;
    err = report_result(&manifest, unchanged ? "unchanged" : "installed",
                        unchanged ? "already_active" : "ok",
                        unchanged ? "The ROM is already active." :
                                    "ROM stored and SRAM verified; press console RESET.");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ROM installed but acknowledgement failed; lease will retry");
        return err;
    }
    *result = CLOUD_QUEUE_INSTALLED;
    ESP_LOGI(TAG, "operator-dispatched ROM installed; press Famicom RESET");
    return ESP_OK;
}

#endif
