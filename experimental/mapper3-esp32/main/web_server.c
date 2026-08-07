#include "web_server.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "ines.h"
#include "mapper_runtime.h"
#include "sdkconfig.h"

static const char *TAG = "web";

static const char INDEX_HTML[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>ROM Vomitter FC</title><style>"
    "body{font:16px system-ui;margin:2rem;max-width:44rem;background:#10151c;color:#edf4ff}"
    "main{background:#1b2430;padding:1.5rem;border-radius:16px}"
    "button{padding:.8rem 1.2rem;font-weight:700} .warn{color:#ffd166}"
    "code{color:#8bd3ff} #status{white-space:pre-wrap}</style></head><body><main>"
    "<h1>ROM Vomitter FC</h1>"
    "<p>Experimental build: mapper 0 (8 KiB CHR) or mapper 3 "
    "(exactly two 8 KiB CHR banks).</p>"
    "<p class=warn><b>Famicom:</b> an upload while playing will freeze the console. "
    "Wait for the blue LED to become solid, then press the console's red RESET button. "
    "The cartridge's ESP RST button does not launch the game.</p>"
    "<input id=file type=file accept='.nes,application/octet-stream'> "
    "<button id=upload>Upload</button><pre id=status>Loading status...</pre>"
    "<script>async function status(){let r=await fetch('/api/status');"
    "document.querySelector('#status').textContent=JSON.stringify(await r.json(),null,2)}"
    "document.querySelector('#upload').onclick=async()=>{let f=document.querySelector('#file').files[0];"
    "if(!f)return alert('Choose a .nes file');"
    "if(!confirm('The Famicom will freeze during reload. Continue?'))return;"
    "let r=await fetch('/api/upload',{method:'POST',body:f});"
    "let t=await r.text();if(!r.ok)alert(t);await status()};status();setInterval(status,2000)</script>"
    "</main></body></html>";

static esp_err_t index_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    controller_status_t status;
    mapper_runtime_stats_t mapper_stats;
    controller_get_status(&status);
    mapper_runtime_get_stats(&mapper_stats);
    char response[512];
    snprintf(response, sizeof(response),
             "{\"mode\":\"%s\",\"console_power\":%s,\"console_exposed\":%s,"
             "\"mapper_runtime\":%s,\"mapper_bank\":%u,"
             "\"mapper_bank0_writes\":%" PRIu32 ","
             "\"mapper_bank1_writes\":%" PRIu32 ","
             "\"mapper_trace_candidates\":%" PRIu32 ","
             "\"mapper_trace_stored\":%" PRIu32 ","
             "\"mapper_trace_saved\":%s,"
             "\"mapper_saved_trace_candidates\":%" PRIu32 ","
             "\"mapper_saved_trace_records\":%" PRIu32 ","
             "\"has_image\":%s,\"sequence\":%" PRIu32
             ",\"crc32\":\"%08" PRIx32 "\","
             "\"message\":\"%s\"}",
             controller_mode_name(status.mode), status.console_power ? "true" : "false",
             status.console_exposed ? "true" : "false",
             mapper_runtime_active() ? "true" : "false",
             mapper_stats.current_bank, mapper_stats.bank0_writes,
             mapper_stats.bank1_writes, mapper_stats.trace_candidates,
             mapper_stats.trace_stored,
             mapper_stats.trace_saved ? "true" : "false",
             mapper_stats.saved_trace_candidates,
             mapper_stats.saved_trace_records,
             status.has_image ? "true" : "false", status.sequence,
             status.image_crc32, status.message != NULL ? status.message : "");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response);
}

static const char *mapper_trace_result_name(uint8_t result)
{
    if (result == MAPPER_TRACE_RESULT_BANK0) {
        return "bank0";
    }
    if (result == MAPPER_TRACE_RESULT_BANK1) {
        return "bank1";
    }
    return "rejected";
}

static esp_err_t mapper_trace_handler(httpd_req_t *request)
{
    mapper_trace_snapshot_t trace;
    if (!mapper_runtime_get_saved_trace(&trace)) {
        httpd_resp_set_status(request, "404 Not Found");
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(
            request,
            "{\"error\":\"no saved trace; play, then press BOOT once\"}");
    }

    uint8_t order[MAPPER_TRACE_RECORD_COUNT];
    const unsigned count =
        trace.stored_count < MAPPER_TRACE_RECORD_COUNT
            ? (unsigned)trace.stored_count
            : MAPPER_TRACE_RECORD_COUNT;
    for (unsigned i = 0; i < count; ++i) {
        order[i] = (uint8_t)i;
    }
    for (unsigned i = 1; i < count; ++i) {
        const uint8_t value = order[i];
        unsigned position = i;
        while (position > 0u &&
               trace.records[order[position - 1u]].ordinal >
                   trace.records[value].ordinal) {
            order[position] = order[position - 1u];
            --position;
        }
        order[position] = value;
    }

    httpd_resp_set_type(request, "application/json");
    char chunk[256];
    snprintf(chunk, sizeof(chunk),
             "{\"cpu_hz\":240000000,\"state_bits\":{\"bit0\":\"CPU_D0\","
             "\"bit1\":\"ROMSEL_INV\",\"bit2\":\"PRG_EN_n\"},"
             "\"total_candidates\":%" PRIu32 ",\"records\":[",
             trace.total_candidates);
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, chunk,
                                               HTTPD_RESP_USE_STRLEN), TAG,
                        "trace header send failed");
    for (unsigned i = 0; i < count; ++i) {
        const mapper_trace_record_t *record = &trace.records[order[i]];
        snprintf(chunk, sizeof(chunk),
                 "%s{\"ordinal\":%" PRIu32 ",\"result\":\"%s\","
                 "\"polls\":%u,\"transitions\":[",
                 i == 0u ? "" : ",", record->ordinal,
                 mapper_trace_result_name(record->result), record->poll_count);
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, chunk,
                                                   HTTPD_RESP_USE_STRLEN), TAG,
                            "trace record send failed");
        const unsigned transitions =
            record->transition_count < MAPPER_TRACE_TRANSITIONS_PER_RECORD
                ? record->transition_count
                : MAPPER_TRACE_TRANSITIONS_PER_RECORD;
        for (unsigned j = 0; j < transitions; ++j) {
            const mapper_trace_transition_t *transition =
                &record->transitions[j];
            const unsigned nanoseconds =
                ((unsigned)transition->delta_cycles * 25u + 3u) / 6u;
            snprintf(chunk, sizeof(chunk),
                     "%s{\"cycles\":%u,\"ns\":%u,\"CPU_D0\":%u,"
                     "\"ROMSEL_INV\":%u,\"PRG_EN_n\":%u}",
                     j == 0u ? "" : ",", transition->delta_cycles,
                     nanoseconds, transition->state & 1u,
                     (transition->state >> 1u) & 1u,
                     (transition->state >> 2u) & 1u);
            ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, chunk,
                                                       HTTPD_RESP_USE_STRLEN), TAG,
                                "trace transition send failed");
        }
        ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, "]}", 2), TAG,
                            "trace record close failed");
    }
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, "]}", 2), TAG,
                        "trace response close failed");
    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t upload_handler(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > NESCART_MAX_INES_SIZE) {
        httpd_resp_set_status(request, "413 Payload Too Large");
        return httpd_resp_sendstr(request, "Invalid or oversized iNES upload");
    }
    uint8_t *upload = malloc((size_t)request->content_len);
    if (upload == NULL) {
        httpd_resp_send_500(request);
        return ESP_ERR_NO_MEM;
    }
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        const int result = httpd_req_recv(request, (char *)upload + received,
                                          request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            free(upload);
            httpd_resp_send_500(request);
            return ESP_FAIL;
        }
        received += (size_t)result;
    }

    char error[160];
    const esp_err_t err = controller_install_ines(upload, received,
                                                   error, sizeof(error));
    free(upload);
    if (err != ESP_OK) {
        httpd_resp_set_status(request, err == ESP_ERR_INVALID_ARG
                                          ? "400 Bad Request"
                                          : "500 Internal Server Error");
        return httpd_resp_sendstr(request, error);
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request,
                              "{\"ok\":true,\"next\":\"Wait for solid LED, then press console RESET\"}");
}

esp_err_t web_server_start(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    if (esp_netif_create_default_wifi_ap() == NULL) {
        return ESP_FAIL;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init failed");

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "MAC read failed");
    wifi_config_t config = {0};
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid),
             "ROM-VOMITTER-%02X%02X", mac[4], mac[5]);
    config.ap.ssid_len = strlen((char *)config.ap.ssid);
    strlcpy((char *)config.ap.password, CONFIG_NESCART_AP_PASSWORD,
            sizeof(config.ap.password));
    config.ap.channel = CONFIG_NESCART_AP_CHANNEL;
    config.ap.max_connection = 4;
    config.ap.authmode = strlen(CONFIG_NESCART_AP_PASSWORD) >= 8
                             ? WIFI_AUTH_WPA2_PSK
                             : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "AP config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");
    esp_err_t power_err = esp_wifi_set_max_tx_power(CONFIG_NESCART_WIFI_TX_POWER_QDBM);
    if (power_err != ESP_OK) {
        ESP_LOGW(TAG, "TX power cap was not applied: %s", esp_err_to_name(power_err));
    }
    (void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 5;
    server_config.stack_size = 8192;
    server_config.core_id = 0;
    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &server_config), TAG, "HTTP server failed");
    const httpd_uri_t index_uri = {
        .uri = "/", .method = HTTP_GET, .handler = index_handler,
    };
    const httpd_uri_t status_uri = {
        .uri = "/api/status", .method = HTTP_GET, .handler = status_handler,
    };
    const httpd_uri_t upload_uri = {
        .uri = "/api/upload", .method = HTTP_POST, .handler = upload_handler,
    };
    const httpd_uri_t trace_uri = {
        .uri = "/api/mapper-trace", .method = HTTP_GET,
        .handler = mapper_trace_handler,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &index_uri), TAG, "index route failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &status_uri), TAG, "status route failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &upload_uri), TAG, "upload route failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &trace_uri), TAG,
                        "mapper trace route failed");
    ESP_LOGI(TAG, "SoftAP %s ready at http://192.168.4.1", config.ap.ssid);
    return ESP_OK;
}
