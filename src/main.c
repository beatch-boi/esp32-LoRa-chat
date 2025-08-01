#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define UART2_BAUDRATE 9600

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_partition.h"
#include "esp_spi_flash.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "hal/uart_types.h"

#define TAG "MESAY"

#define SOFTAP_SSID      "ESP32 LoRa Chat 1"
#define SOFTAP_PASSWORD  "VoltiqSmart"
#define SOFTAP_CHANNEL   1
#define SOFTAP_MAX_CONN  1

void wifi_init_softap(void);
httpd_handle_t start_web_server(void);
void vfs_init_file_system(void);
void uart2_init(void);

int socket_fd = 0;

static httpd_handle_t server_handle;

static void uart2_recive(void *arg) {
    #define RX_BUF_SIZE 1024 * 2
    uint8_t *data = (uint8_t*)malloc(RX_BUF_SIZE + 1);

    while (1) {
        const int rx_bytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        
        if (rx_bytes > 0) {

            data[rx_bytes] = 0;

            ESP_LOGI(TAG, "Uart2 says: %s", (char*)data);    

            httpd_ws_frame_t ws_pkt;
            memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
            ws_pkt.type = HTTPD_WS_TYPE_TEXT;
            ws_pkt.payload = (uint8_t*)data;
            ws_pkt.len = strlen((char*)data);

            httpd_ws_send_frame_async(server_handle, socket_fd, &ws_pkt);
        }
    }
    free(data);
}

void app_main() {
    uart2_init();

    vfs_init_file_system();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_softap();

    server_handle = start_web_server();

    xTaskCreate(uart2_recive, "uart2_rx_task", 1024 * 2, NULL, configMAX_PRIORITIES - 1, NULL);

    //TaskStartScheduler();
}

void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    (void)esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid           = SOFTAP_SSID,
            .ssid_len       = strlen(SOFTAP_SSID),
            .channel        = SOFTAP_CHANNEL,
            .password       = SOFTAP_PASSWORD,
            .max_connection = SOFTAP_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:\"%s\" password:\"%s\" channel:\"%d\"", SOFTAP_SSID, SOFTAP_PASSWORD, SOFTAP_CHANNEL);
}

esp_err_t read_file_to_string(char **buffer, const char *fname, const char *mode)
{
    FILE *f = fopen(fname, mode);
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Error: Failed to open file  %s as %s", fname, mode);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    rewind(f);

    *buffer = (char *)malloc((flen + 1) * sizeof(char));
    fread(*buffer, flen, 1, f);

    (*buffer)[flen] = '\0';

    fclose(f);

    return ESP_OK;
}

esp_err_t chat_page_handler(httpd_req_t *req) {
    char *buffer = NULL;

    ESP_ERROR_CHECK(httpd_resp_set_type(req, "text/html"));
    ESP_ERROR_CHECK(read_file_to_string(&buffer, "/storage/chat.html", "rb"));

    ESP_ERROR_CHECK(httpd_resp_send(req, buffer, strlen(buffer)));

    free(buffer);

    return ESP_OK;
}

httpd_uri_t uri_chat = {
    .uri = "/chat",
    .method = HTTP_GET,
    .handler = chat_page_handler,
    .user_ctx = NULL,
};

esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Websocket client connected.");
        socket_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    //uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
 
    if (ws_pkt.len != 0) {
        //buf = calloc(1, ws_pkt.len + 1);
        //if (buf == NULL) {
        //    ESP_LOGE(TAG, "Failed to calloc memory for buf");
        //    return ESP_ERR_NO_MEM;
        //}
        //ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            //free(buf);
            return ret;
        }

        //ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);

        char *tx_data = (char*)ws_pkt.payload;
        int len = strlen(tx_data);
        uart_write_bytes(UART_NUM_2, tx_data, len);
    }

    // ret = httpd_ws_send_frame(req, &ws_pkt);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    // }

    //free(buf);

    return ret;
}

httpd_uri_t uri_ws = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = false, 
    .supported_subprotocol = "chat"
};

httpd_handle_t start_web_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_chat));
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri_ws));
    }

    return server;
}

void vfs_init_file_system(void) {
    esp_vfs_spiffs_conf_t cfg = {
        .base_path = "/storage",
        .format_if_mount_failed = false,
        .max_files = 10,
        .partition_label = NULL
    };

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&cfg));
}

void uart2_init(void) {
    uart_config_t uart_config = {
        .baud_rate = UART2_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 1024 * 2, 0, 0, NULL, 0));
}