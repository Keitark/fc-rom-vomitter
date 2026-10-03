#include "web_server.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "controller.h"
#include "cloud_sync.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "ines.h"
#include "nes_sdr_platform.h"
#include "sdkconfig.h"
#include "usb_loader.h"

static const char *TAG = "web";
static httpd_handle_t s_server;

static const char INDEX_HTML[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>ROM Vomitter FC</title><style>"
    "body{font:16px system-ui;margin:2rem;max-width:44rem;background:#10151c;color:#edf4ff}"
    "main{background:#1b2430;padding:1.5rem;border-radius:16px}"
    "button{padding:.8rem 1.2rem;font-weight:700} .warn{color:#ffd166}"
    "code{color:#8bd3ff} #status{white-space:pre-wrap}</style></head><body><main>"
    "<h1>ROM Vomitter FC</h1>"
    "<p>Upload a mapper-0 iNES image: 16/32 KiB PRG and 8 KiB CHR.</p>"
    "<p class=warn><b>Famicom:</b> an upload while playing will freeze the console. "
    "Wait for the blue LED to become solid, then press the console's red RESET button. "
    "The cartridge's ESP RST button does not launch the game.</p>"
    "<input id=file type=file accept='.nes,application/octet-stream'> "
    "<button id=upload>Upload</button> "
    "<button id=nesdemo>NES-SDR demo frame</button> "
    "<button id=nesrf>Start SDR mode</button>"
    "<p class=warn>NES-SDR demo rewrites live CHR. Start SDR mode disconnects this Wi-Fi AP "
    "and runs until the cartridge ESP is reset.</p>"
    "<pre id=status>Loading status...</pre>"
    "<script>let statusBusy=false,rfStarting=false;"
    "async function status(){if(statusBusy||rfStarting)return;statusBusy=true;"
    "let c=new AbortController(),timer=setTimeout(()=>c.abort(),4000);"
    "try{let r=await fetch('/api/status',{signal:c.signal,cache:'no-store'});"
    "if(!r.ok)throw Error('HTTP '+r.status);"
    "document.querySelector('#status').textContent=JSON.stringify(await r.json(),null,2)}"
    "catch(e){document.querySelector('#status').textContent='Connection interrupted; retrying...'}"
    "finally{clearTimeout(timer);statusBusy=false}}"
    "document.querySelector('#upload').onclick=async()=>{let f=document.querySelector('#file').files[0];"
    "if(!f)return alert('Choose a .nes file');"
    "if(!confirm('The Famicom will freeze during reload. Continue?'))return;"
    "let r=await fetch('/api/upload',{method:'POST',body:f});"
    "let t=await r.text();if(!r.ok)alert(t);await status()};"
    "document.querySelector('#nesdemo').onclick=async()=>{"
    "if(!confirm('Rewrite the NES-SDR graph CHR now?'))return;"
    "let r=await fetch('/api/nes-sdr/demo-frame',{method:'POST'});"
    "let t=await r.text();if(!r.ok)alert(t);await status()};"
    "document.querySelector('#nesrf').onclick=async()=>{"
    "if(!confirm('Start exclusive SDR mode? This Wi-Fi AP will disconnect.'))return;"
    "let r=await fetch('/api/nes-sdr/start-rf',{method:'POST'});"
    "let t=await r.text();if(!r.ok)return alert(t);rfStarting=true;"
    "document.querySelector('#status').textContent='SDR mode starting; Wi-Fi will disconnect.'};"
    "status();setInterval(status,5000)</script>"
    "</main></body></html>";

static esp_err_t index_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    controller_status_t status;
    controller_get_status(&status);
    char response[768];
    snprintf(response, sizeof(response),
             "{\"mode\":\"%s\",\"console_power\":%s,\"console_exposed\":%s,"
             "\"has_image\":%s,\"sequence\":%" PRIu32 ",\"crc32\":\"%08" PRIx32 "\","
             "\"usb_upload\":%s,\"cloud_pull\":%s,\"cloud_status\":\"%s\","
             "\"nes_sdr_rf_available\":%s,\"nes_sdr_rf_state\":\"%s\","
             "\"message\":\"%s\"}",
             controller_mode_name(status.mode), status.console_power ? "true" : "false",
             status.console_exposed ? "true" : "false",
             status.has_image ? "true" : "false", status.sequence,
             status.image_crc32,
             usb_loader_enabled() ? "true" : "false",
             cloud_sync_enabled() ? "true" : "false",
             cloud_sync_status_name(),
             nes_sdr_platform_rf_available() ? "true" : "false",
             nes_sdr_platform_rf_state_name(),
             status.message != NULL ? status.message : "");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response);
}

typedef struct {
    httpd_req_t *request;
    bool read_failed;
} upload_reader_t;

static int upload_read_exact(void *context, uint8_t *output, size_t length)
{
    upload_reader_t *reader = context;
    size_t received = 0;
    unsigned consecutive_timeouts = 0;
    while (received < length) {
        const size_t remaining = length - received;
        const size_t chunk = remaining < 1024u ? remaining : 1024u;
        const int result = httpd_req_recv(reader->request,
                                          (char *)output + received, chunk);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++consecutive_timeouts < 3u) {
                continue;
            }
            reader->read_failed = true;
            return -1;
        }
        if (result <= 0) {
            reader->read_failed = true;
            return -1;
        }
        consecutive_timeouts = 0;
        received += (size_t)result;
    }
    return 0;
}

static esp_err_t upload_handler(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > NESCART_MAX_INES_SIZE) {
        httpd_resp_set_status(request, "413 Payload Too Large");
        return httpd_resp_sendstr(request, "Invalid or oversized iNES upload");
    }
    upload_reader_t reader = {.request = request};
    char error[160];
    const esp_err_t err = controller_install_ines_stream(
        upload_read_exact, &reader, (size_t)request->content_len,
        error, sizeof(error));
    if (err != ESP_OK) {
        httpd_resp_set_status(request, reader.read_failed
                                          ? "408 Request Timeout"
                                          : err == ESP_ERR_INVALID_ARG
                                                ? "400 Bad Request"
                                                : "500 Internal Server Error");
        return httpd_resp_sendstr(request, error);
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request,
                              "{\"ok\":true,\"next\":\"Wait for solid LED, then press console RESET\"}");
}

static esp_err_t nes_sdr_demo_handler(httpd_req_t *request)
{
    controller_status_t status;
    controller_get_status(&status);
    if (!status.has_image || !status.console_power) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(
            request, "Load NES-SDR and power the Famicom before refreshing CHR");
    }
    if (!nes_sdr_platform_image_supported()) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(
            request, "Installed ROM is not a signed NES-SDR image");
    }

    const esp_err_t err = nes_sdr_platform_demo_step();
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, esp_err_to_name(err));
    }

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(
        request, "{\"ok\":true,\"bytes\":3072,\"source\":\"synthetic-spc1\"}");
}

static esp_err_t nes_sdr_start_rf_handler(httpd_req_t *request)
{
    controller_status_t status;
    controller_get_status(&status);
    if (!status.has_image || !status.console_power) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(
            request, "Load signed NES-SDR and power the Famicom first");
    }
    if (!nes_sdr_platform_image_supported()) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(
            request, "Installed ROM is not a signed NES-SDR image");
    }
    if (cloud_sync_enabled()) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(
            request, "Disable cloud-pull mode before entering exclusive SDR mode");
    }
    if (!nes_sdr_platform_rf_available()) {
        httpd_resp_set_status(request, "501 Not Implemented");
        return httpd_resp_sendstr(
            request, "ESP-SDR RF backend is not linked in this build");
    }

    const esp_err_t err = nes_sdr_platform_start_rf();
    if (err != ESP_OK) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, esp_err_to_name(err));
    }

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(
        request,
        "{\"ok\":true,\"next\":\"SoftAP will disconnect; reset ESP to stop SDR mode\"}");
}

esp_err_t web_server_stop_for_sdr(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }
    const esp_err_t err = httpd_stop(s_server);
    if (err == ESP_OK) {
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped for exclusive SDR mode");
    }
    return err;
}

esp_err_t web_server_start(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");
    if (esp_netif_create_default_wifi_ap() == NULL) {
        return ESP_FAIL;
    }
#if CONFIG_NESCART_CLOUD_PULL_ENABLE
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }
#endif
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

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(
                            cloud_sync_enabled() ? WIFI_MODE_APSTA : WIFI_MODE_AP),
                        TAG, "Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "AP config failed");
    ESP_RETURN_ON_ERROR(cloud_sync_prepare_wifi(), TAG, "cloud Wi-Fi setup failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");
    ESP_RETURN_ON_ERROR(cloud_sync_start(), TAG, "cloud task start failed");
    esp_err_t power_err = esp_wifi_set_max_tx_power(CONFIG_NESCART_WIFI_TX_POWER_QDBM);
    if (power_err != ESP_OK) {
        ESP_LOGW(TAG, "TX power cap was not applied: %s", esp_err_to_name(power_err));
    }
    (void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 6;
    server_config.stack_size = 8192;
    server_config.lru_purge_enable = true;
    s_server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &server_config), TAG, "HTTP server failed");
    const httpd_uri_t index_uri = {
        .uri = "/", .method = HTTP_GET, .handler = index_handler,
    };
    const httpd_uri_t status_uri = {
        .uri = "/api/status", .method = HTTP_GET, .handler = status_handler,
    };
    const httpd_uri_t upload_uri = {
        .uri = "/api/upload", .method = HTTP_POST, .handler = upload_handler,
    };
    const httpd_uri_t nes_sdr_demo_uri = {
        .uri = "/api/nes-sdr/demo-frame",
        .method = HTTP_POST,
        .handler = nes_sdr_demo_handler,
    };
    const httpd_uri_t nes_sdr_start_rf_uri = {
        .uri = "/api/nes-sdr/start-rf",
        .method = HTTP_POST,
        .handler = nes_sdr_start_rf_handler,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &index_uri), TAG, "index route failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &status_uri), TAG, "status route failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &upload_uri), TAG, "upload route failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &nes_sdr_demo_uri),
                        TAG, "NES-SDR demo route failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &nes_sdr_start_rf_uri),
                        TAG, "NES-SDR RF route failed");
    ESP_LOGI(TAG, "SoftAP %s ready at http://192.168.4.1", config.ap.ssid);
    return ESP_OK;
}
