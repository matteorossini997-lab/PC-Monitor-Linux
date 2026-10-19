#include "power_network.h"
#include "sdkconfig.h"

#if CONFIG_BC250_WIFI
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "power_controller.h"
#include "power_security.h"
#include "drivers/usb_serial_comm.h"

static const char *TAG = "BC250-HTTPS";
static httpd_handle_t s_server;
static esp_timer_handle_t s_retry;
extern const unsigned char cert_start[] asm("_binary_servercert_pem_start");
extern const unsigned char cert_end[] asm("_binary_servercert_pem_end");
extern const unsigned char key_start[] asm("_binary_prvtkey_pem_start");
extern const unsigned char key_end[] asm("_binary_prvtkey_pem_end");
extern const unsigned char page_start[] asm("_binary_power_html_start");

static void response_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

static bool authorized(httpd_req_t *req)
{
    char header[80];
    if (httpd_req_get_hdr_value_len(req, "Authorization") != 71 ||
        httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK) return false;
    return bc_authorized(CONFIG_BC250_API_TOKEN, header);
}

static esp_err_t json_reply(httpd_req_t *req, const char *status, const char *body)
{
    response_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_handler(httpd_req_t *req)
{
    if (!authorized(req)) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        return json_reply(req, "401 Unauthorized", "{\"error\":\"unauthorized\"}");
    }
    if (req->content_len != 0) {
        httpd_resp_set_hdr(req, "Connection", "close");
        return json_reply(req, "400 Bad Request", "{\"error\":\"body_not_allowed\"}");
    }
    if (req->method == HTTP_GET) {
        char status[256];
        if (!bc250_power_status_json(status, sizeof(status)))
            return json_reply(req, "503 Service Unavailable", "{\"error\":\"busy\"}");
        return json_reply(req, "200 OK", status);
    }
    if (!bc250_power_submit((bc_command_t)(intptr_t)req->user_ctx))
        return json_reply(req, "503 Service Unavailable", "{\"error\":\"queue_full\"}");
    return json_reply(req, "202 Accepted", "{\"queued\":true}");
}

static esp_err_t page_handler(httpd_req_t *req)
{
    response_headers(req);
    httpd_resp_set_hdr(req, "Content-Security-Policy",
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
        "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)page_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_server(void)
{
    if (s_server) return ESP_OK;
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = cert_start;
    config.servercert_len = cert_end - cert_start;
    config.prvtkey_pem = key_start;
    config.prvtkey_len = key_end - key_start;
    config.httpd.max_open_sockets = 3;
    ESP_RETURN_ON_ERROR(httpd_ssl_start(&s_server, &config), TAG, "HTTPS start");
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = page_handler };
    esp_err_t result = httpd_register_uri_handler(s_server, &root);
    const struct { const char *uri; httpd_method_t method; bc_command_t command; } routes[] = {
        {"/api/status", HTTP_GET, BC_CMD_START},
        {"/api/start", HTTP_POST, BC_CMD_START}, {"/api/stop", HTTP_POST, BC_CMD_STOP},
        {"/api/sleep", HTTP_POST, BC_CMD_SLEEP}, {"/api/wake", HTTP_POST, BC_CMD_WAKE},
        {"/api/confirm-off", HTTP_POST, BC_CMD_CONFIRM_OFF}
    };
    for (size_t i = 0; result == ESP_OK && i < sizeof(routes) / sizeof(routes[0]); ++i) {
        httpd_uri_t route = { .uri = routes[i].uri, .method = routes[i].method,
            .handler = api_handler, .user_ctx = (void *)(intptr_t)routes[i].command };
        result = httpd_register_uri_handler(s_server, &route);
    }
    if (result != ESP_OK) {
        httpd_ssl_stop(s_server);
        s_server = NULL;
    }
    return result;
}

static void retry_wifi(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        /* A network failure never changes the power state. Bounded retry rate. */
        esp_timer_stop(s_retry);
        esp_timer_start_once(s_retry, 5000000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        esp_timer_stop(s_retry);
        esp_netif_set_hostname(event->esp_netif, "bc250-controller");
        ESP_LOGI(TAG, "LAN control: https://" IPSTR "/", IP2STR(&event->ip_info.ip));
        usb_serial_sendf("PWR_NET:https://" IPSTR "/\n", IP2STR(&event->ip_info.ip));
        if (start_server() != ESP_OK) ESP_LOGE(TAG, "HTTPS unavailable; power policy unchanged");
    }
}

esp_err_t bc250_network_start(void)
{
    size_t ssid_len = strlen(CONFIG_BC250_WIFI_SSID), pass_len = strlen(CONFIG_BC250_WIFI_PASSWORD);
    if (!ssid_len || ssid_len > 32 || pass_len < 8 || pass_len > 63 ||
        !bc_token_valid(CONFIG_BC250_API_TOKEN)) return ESP_ERR_INVALID_ARG;
    /* Never erase NVS automatically: other existing project settings may live there. */
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "NVS init (not erased on error)");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "network init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    if (!station) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_netif_set_hostname(station, "bc250-controller"), TAG, "hostname");
    esp_timer_create_args_t timer = { .callback = retry_wifi, .name = "bc250_wifi_retry" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer, &s_retry), TAG, "retry timer");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL), TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL), TAG, "IP events");
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, CONFIG_BC250_WIFI_SSID, ssid_len);
    memcpy(wifi.sta.password, CONFIG_BC250_WIFI_PASSWORD, pass_len);
    wifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi), TAG, "Wi-Fi configuration");
    return esp_wifi_start();
}
#else
esp_err_t bc250_network_start(void) { return ESP_OK; }
#endif
