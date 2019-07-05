/*
 * ESP32 WiFi connect sample
  *
 * Copyright (C) 2018 KIMATA Tetsuya <kimata@green-rabbit.net>
 *
 * This program is free software ; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

#include "nvs_flash.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "esp_event_loop.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include <string.h>

#include "wifi_config.h"
// wifi_config.h should define followings.
// #define WIFI_SSID "XXXXXXXX"            // WiFi SSID
// #define WIFI_PASS "XXXXXXXX"            // WiFi Password

#define WIFI_HOSTNAME "ESP32-wifi_connect" // module's hostname
#define WIFI_CONNECT_TIMEOUT 3

#define TAG "WIFI-SAMPLE"

SemaphoreHandle_t wifi_conn_done = NULL;

//////////////////////////////////////////////////////////////////////
// Error Handling
static void _error_check_failed(esp_err_t rc, const char *file, int line,
                                const char *function, const char *expression)
{
    ets_printf("ESP_ERROR_CHECK failed: esp_err_t 0x%x", rc);
#ifdef CONFIG_ESP_ERR_TO_NAME_LOOKUP
    ets_printf(" (%s)", esp_err_to_name(rc));
#endif //CONFIG_ESP_ERR_TO_NAME_LOOKUP
    ets_printf(" at 0x%08x\n", (intptr_t)__builtin_return_address(0) - 3);
    if (spi_flash_cache_enabled()) { // strings may be in flash cache
        ets_printf("file: \"%s\" line %d\nfunc: %s\nexpression: %s\n", file, line, function, expression);
    }
}

#define ERROR_RETURN(x, fail) do {                                      \
        esp_err_t __err_rc = (x);                                       \
        if (__err_rc != ESP_OK) {                                       \
            _error_check_failed(__err_rc, __FILE__, __LINE__,           \
                                __ASSERT_FUNC, #x);                     \
            return fail;                                                \
        }                                                               \
    } while(0);

//////////////////////////////////////////////////////////////////////
// Wifi Function
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        ERROR_RETURN(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, WIFI_HOSTNAME), ESP_FAIL);
        ERROR_RETURN(esp_wifi_connect(), ESP_FAIL);
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_CONNECTED");
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        xSemaphoreGive(wifi_conn_done);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        xSemaphoreGive(wifi_conn_done);
        break;
    case SYSTEM_EVENT_STA_STOP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_STOP");
        break;
    default:
        break;
    }
    return ESP_OK;
}

static bool wifi_init()
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ERROR_RETURN(nvs_flash_erase(), false);
        ret = nvs_flash_init();
    }
    ERROR_RETURN(ret, false);

    tcpip_adapter_init();

    ERROR_RETURN(esp_event_loop_init(event_handler, NULL), false);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ERROR_RETURN(esp_wifi_init(&cfg), false);
    ERROR_RETURN(esp_wifi_set_storage(WIFI_STORAGE_RAM), false);
    ERROR_RETURN(esp_wifi_set_mode(WIFI_MODE_STA), false);

#ifdef WIFI_SSID
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    wifi_config_t wifi_config_cur;
    ERROR_RETURN(esp_wifi_get_config(WIFI_IF_STA, &wifi_config_cur), false);

    if (strcmp((const char *)wifi_config_cur.sta.ssid, (const char *)wifi_config.sta.ssid) ||
        strcmp((const char *)wifi_config_cur.sta.password, (const char *)wifi_config.sta.password)) {
        ESP_LOGI(TAG, "SAVE WIFI CONFIG");
        ERROR_RETURN(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config), false);
    }
#endif

    return true;
}

static bool wifi_connect(wifi_ap_record_t *ap_info)
{
    xSemaphoreTake(wifi_conn_done, portMAX_DELAY);
    ERROR_RETURN(esp_wifi_start(), false);

    if (xSemaphoreTake(wifi_conn_done, WIFI_CONNECT_TIMEOUT * 1000 / portTICK_RATE_MS) == pdTRUE) {
        ERROR_RETURN(esp_wifi_sta_get_ap_info(ap_info), false);
        return true;
    } else {
        ESP_LOGE(TAG, "WIFI CONNECT TIMECOUT");
        return false;
    }
}

static bool wifi_stop()
{
    esp_wifi_disconnect();
    esp_wifi_stop();

    return true;
}


//////////////////////////////////////////////////////////////////////
void app_main()
{
    uint32_t sleep_sec = 60;
    wifi_ap_record_t ap_info;
    vSemaphoreCreateBinary(wifi_conn_done);

    esp_log_level_set("wifi", ESP_LOG_ERROR);

    uint32_t time_start = xTaskGetTickCount();
    if (wifi_init() && wifi_connect(&ap_info)) {
        uint32_t connect_msec = (xTaskGetTickCount() - time_start) * portTICK_PERIOD_MS;
        ESP_LOGI(TAG, "CONN TIME: %d ms", connect_msec);

        vTaskDelay(100 / portTICK_RATE_MS); // Do something
    }
    wifi_stop();

    ESP_LOGI(TAG, "Sleep %d sec", sleep_sec);

    vTaskDelay(20 / portTICK_RATE_MS); // wait 20ms for flush UART

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sleep_sec * 1000000LL));
    esp_deep_sleep_start();
}
