#include "cloud_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"
#include "cloud_queue.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "ines.h"
#include "sdkconfig.h"

#if CONFIG_NESCART_CLOUD_PULL_ENABLE

#ifndef CONFIG_NESCART_CLOUD_ALLOW_CONSOLE_RELOAD
#define CONFIG_NESCART_CLOUD_ALLOW_CONSOLE_RELOAD 0
#endif

static const char *TAG = "cloud_sync";

#define CLOUD_CONNECTED_BIT BIT0

#if CONFIG_NESCART_CLOUD_QUEUE_ENABLE
static bool valid_device_id(const char *value)
{
    const size_t length = strlen(value);
    if (length == 0 || length > 64) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}
#endif

typedef enum {
    CLOUD_WAITING_WIFI,
    CLOUD_WAITING_TIME,
    CLOUD_IDLE,
    CLOUD_DEFERRED_CONSOLE,
    CLOUD_DOWNLOADING,
    CLOUD_INSTALLED,
    CLOUD_ERROR,
} cloud_state_t;

static EventGroupHandle_t s_wifi_events;
static volatile cloud_state_t s_state = CLOUD_WAITING_WIFI;
static bool s_sntp_started;

static bool url_is_allowed(void)
{
#if CONFIG_NESCART_CLOUD_QUEUE_ENABLE
    const char *url = CONFIG_NESCART_CLOUD_SERVICE_ORIGIN;
#else
    const char *url = CONFIG_NESCART_CLOUD_ROM_URL;
#endif
    if (strncmp(url, "https://", 8) == 0) {
        return true;
    }
#if CONFIG_NESCART_CLOUD_ALLOW_INSECURE_HTTP
    return strncmp(url, "http://", 7) == 0;
#else
    return false;
#endif
}

static void wifi_event(void *context, esp_event_base_t base,
                       int32_t event_id, void *event_data)
{
    (void)context;
    (void)event_data;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, CLOUD_CONNECTED_BIT);
        s_state = CLOUD_WAITING_WIFI;
        (void)esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, CLOUD_CONNECTED_BIT);
    }
}

#if !CONFIG_NESCART_CLOUD_QUEUE_ENABLE
static esp_err_t fetch_and_install(void)
{
    controller_status_t controller;
    controller_get_status(&controller);
    if (controller.console_power && !CONFIG_NESCART_CLOUD_ALLOW_CONSOLE_RELOAD) {
        s_state = CLOUD_DEFERRED_CONSOLE;
        ESP_LOGI(TAG, "cloud update deferred while console power is present");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *payload = malloc(NESCART_MAX_INES_SIZE);
    if (payload == NULL) {
        s_state = CLOUD_ERROR;
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_NESCART_CLOUD_ROM_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = CONFIG_NESCART_CLOUD_HTTP_TIMEOUT_MS,
        .buffer_size = 1024,
        .user_agent = "fc-rom-vomitter/1 cloud-pull",
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(payload);
        s_state = CLOUD_ERROR;
        return ESP_ERR_NO_MEM;
    }
    (void)esp_http_client_set_header(client, "Accept", "application/octet-stream");
    if (strlen(CONFIG_NESCART_CLOUD_BEARER_TOKEN) != 0) {
        char authorization[320];
        const int written = snprintf(authorization, sizeof(authorization),
                                     "Bearer %s", CONFIG_NESCART_CLOUD_BEARER_TOKEN);
        if (written <= 0 || (size_t)written >= sizeof(authorization)) {
            esp_http_client_cleanup(client);
            free(payload);
            s_state = CLOUD_ERROR;
            return ESP_ERR_INVALID_SIZE;
        }
        (void)esp_http_client_set_header(client, "Authorization", authorization);
    }

    s_state = CLOUD_DOWNLOADING;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        goto cleanup;
    }
    const int64_t declared_length = esp_http_client_fetch_headers(client);
    if (declared_length > (int64_t)NESCART_MAX_INES_SIZE) {
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "cloud endpoint returned HTTP %d", status);
        err = ESP_FAIL;
        goto cleanup;
    }

    size_t received = 0;
    while (received < NESCART_MAX_INES_SIZE) {
        const int count = esp_http_client_read(
            client, (char *)payload + received, NESCART_MAX_INES_SIZE - received);
        if (count < 0) {
            err = ESP_FAIL;
            goto cleanup;
        }
        if (count == 0) {
            break;
        }
        received += (size_t)count;
    }
    if (!esp_http_client_is_complete_data_received(client) || received == 0 ||
        (declared_length >= 0 && received != (size_t)declared_length)) {
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    char install_error[160];
    err = controller_install_ines(payload, received,
                                  install_error, sizeof(install_error));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cloud image rejected: %s", install_error);
        goto cleanup;
    }
    s_state = CLOUD_INSTALLED;
    ESP_LOGI(TAG, "cloud image accepted (%u bytes)", (unsigned)received);

cleanup:
    (void)esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(payload);
    if (err != ESP_OK && s_state != CLOUD_DEFERRED_CONSOLE) {
        s_state = CLOUD_ERROR;
        ESP_LOGW(TAG, "cloud poll failed: %s", esp_err_to_name(err));
    }
    return err;
}
#endif

static esp_err_t ensure_trusted_time(void)
{
    if (!s_sntp_started) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&config), TAG,
                            "SNTP initialization failed");
        s_sntp_started = true;
    }
    s_state = CLOUD_WAITING_TIME;
    return esp_netif_sntp_sync_wait(
        pdMS_TO_TICKS(CONFIG_NESCART_CLOUD_HTTP_TIMEOUT_MS));
}

static void cloud_task(void *context)
{
    (void)context;
    for (;;) {
        const EventBits_t bits = xEventGroupWaitBits(
            s_wifi_events, CLOUD_CONNECTED_BIT, pdFALSE, pdTRUE,
            pdMS_TO_TICKS(CONFIG_NESCART_CLOUD_POLL_SECONDS * 1000));
        if ((bits & CLOUD_CONNECTED_BIT) == 0) {
            s_state = CLOUD_WAITING_WIFI;
            continue;
        }
        const esp_err_t time_err = ensure_trusted_time();
        if (time_err != ESP_OK) {
            ESP_LOGW(TAG, "waiting for trusted time before HTTPS: %s",
                     esp_err_to_name(time_err));
            vTaskDelay(pdMS_TO_TICKS(CONFIG_NESCART_CLOUD_POLL_SECONDS * 1000));
            continue;
        }
        s_state = CLOUD_IDLE;
#if CONFIG_NESCART_CLOUD_QUEUE_ENABLE
        cloud_queue_result_t result = CLOUD_QUEUE_WAITING;
        const esp_err_t err = cloud_queue_poll(&result);
        if (err != ESP_OK) {
            s_state = CLOUD_ERROR;
            ESP_LOGW(TAG, "queue poll failed: %s", esp_err_to_name(err));
        } else if (result == CLOUD_QUEUE_DEFERRED) {
            s_state = CLOUD_DEFERRED_CONSOLE;
        } else if (result == CLOUD_QUEUE_INSTALLED) {
            s_state = CLOUD_INSTALLED;
        }
#else
        (void)fetch_and_install();
#endif
        vTaskDelay(pdMS_TO_TICKS(CONFIG_NESCART_CLOUD_POLL_SECONDS * 1000));
    }
}

bool cloud_sync_enabled(void)
{
    return true;
}

const char *cloud_sync_status_name(void)
{
    switch (s_state) {
    case CLOUD_WAITING_WIFI: return "waiting_wifi";
    case CLOUD_WAITING_TIME: return "waiting_time";
    case CLOUD_IDLE: return "idle";
    case CLOUD_DEFERRED_CONSOLE: return "deferred_console_power";
    case CLOUD_DOWNLOADING: return "downloading";
    case CLOUD_INSTALLED: return "installed";
    case CLOUD_ERROR: return "error";
    default: return "unknown";
    }
}

esp_err_t cloud_sync_prepare_wifi(void)
{
    const size_t password_length = strlen(CONFIG_NESCART_CLOUD_STA_PASSWORD);
#if CONFIG_NESCART_CLOUD_QUEUE_ENABLE
    const char *url = CONFIG_NESCART_CLOUD_SERVICE_ORIGIN;
    const char *host = strncmp(url, "https://", 8) == 0 ? url + 8 :
                       strncmp(url, "http://", 7) == 0 ? url + 7 : url;
    const bool credentials_ok =
        valid_device_id(CONFIG_NESCART_CLOUD_DEVICE_ID) &&
        strlen(CONFIG_NESCART_CLOUD_DEVICE_HMAC_SECRET) >= 32 &&
        strlen(CONFIG_NESCART_CLOUD_DEVICE_HMAC_SECRET) <= 256 &&
        *host != '\0' && strpbrk(host, "/?#@") == NULL;
#else
    const char *url = CONFIG_NESCART_CLOUD_ROM_URL;
    const bool credentials_ok = true;
#endif
    if (strlen(CONFIG_NESCART_CLOUD_STA_SSID) == 0 ||
        strlen(url) == 0 || !url_is_allowed() || !credentials_ok ||
        (password_length != 0 && password_length < 8)) {
        ESP_LOGE(TAG, "cloud mode requires SSID, valid password, endpoint and device credentials");
        return ESP_ERR_INVALID_ARG;
    }
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(esp_event_handler_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL),
                        TAG, "Wi-Fi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL),
                        TAG, "IP event handler failed");

    wifi_config_t station = {0};
    strlcpy((char *)station.sta.ssid, CONFIG_NESCART_CLOUD_STA_SSID,
            sizeof(station.sta.ssid));
    strlcpy((char *)station.sta.password, CONFIG_NESCART_CLOUD_STA_PASSWORD,
            sizeof(station.sta.password));
    station.sta.threshold.authmode = password_length >= 8
                                         ? WIFI_AUTH_WPA2_PSK
                                         : WIFI_AUTH_OPEN;
    station.sta.pmf_cfg.capable = true;
    station.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &station);
}

esp_err_t cloud_sync_start(void)
{
    /* Keep network work off core 1, which the experimental mapper runtime
     * reserves for its timing-critical GPIO loop when that firmware is merged. */
    if (xTaskCreatePinnedToCore(cloud_task, "cloud_rom", 10240, NULL, 4,
                                NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else

bool cloud_sync_enabled(void)
{
    return false;
}

const char *cloud_sync_status_name(void)
{
    return "disabled";
}

esp_err_t cloud_sync_prepare_wifi(void)
{
    return ESP_OK;
}

esp_err_t cloud_sync_start(void)
{
    return ESP_OK;
}

#endif
