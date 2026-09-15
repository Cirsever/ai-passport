#include "passport_transport_tcp.h"

#include "demo_radio.h"
#include "passport_line.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *TAG = "passport_tcp";
#define PASSPORT_TCP_QUEUE_DEPTH 8
#define PASSPORT_TCP_TASK_STACK 6144
#define PASSPORT_TCP_POLL_MS 100

typedef struct {
    size_t length;
    char data[PASSPORT_LINE_MAX + 1];
} transport_line_t;

static QueueHandle_t s_rx_queue;
static QueueHandle_t s_tx_queue;
static SemaphoreHandle_t s_stopped;
static TaskHandle_t s_task;
static esp_netif_t *s_ap_netif;
static volatile bool s_stop_requested;
static volatile bool s_connected;
static bool s_wifi_started;
static bool s_started;

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static bool enqueue_rx(const char *line, size_t length, void *context) {
    (void)context;
    transport_line_t item = { .length = length };
    memcpy(item.data, line, length);
    item.data[length] = '\0';
    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full; dropping one line");
    }
    return true;
}

static void close_client(int *client_fd) {
    if (*client_fd >= 0) close(*client_fd);
    *client_fd = -1;
    s_connected = false;
}

static void drain_tx(int client_fd) {
    transport_line_t item;
    while (client_fd >= 0 && xQueueReceive(s_tx_queue, &item, 0) == pdTRUE) {
        size_t sent = 0;
        item.data[item.length++] = '\n';
        while (sent < item.length) {
            ssize_t result = send(client_fd, item.data + sent, item.length - sent, 0);
            if (result <= 0) return;
            sent += (size_t)result;
        }
    }
}

static void tcp_task(void *argument) {
    (void)argument;
    int listen_fd = -1;
    int client_fd = -1;
    passport_line_buffer_t line_buffer;
    passport_line_init(&line_buffer);

    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_fd < 0 || set_nonblocking(listen_fd) != 0) goto cleanup;

    int reuse = 1;
    (void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(PASSPORT_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listen_fd, 1) != 0) goto cleanup;

    ESP_LOGI(TAG, "listening on %d; join %s and connect to 192.168.4.1:%d",
             PASSPORT_TCP_PORT, PASSPORT_TCP_SSID, PASSPORT_TCP_PORT);

    while (!s_stop_requested) {
        if (client_fd < 0) {
            struct sockaddr_storage peer;
            socklen_t peer_length = sizeof(peer);
            client_fd = accept(listen_fd, (struct sockaddr *)&peer, &peer_length);
            if (client_fd >= 0) {
                if (set_nonblocking(client_fd) != 0) {
                    close_client(&client_fd);
                } else {
                    s_connected = true;
                    passport_line_init(&line_buffer);
                    ESP_LOGI(TAG, "host connected");
                }
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGW(TAG, "accept failed: %d", errno);
            }
        }

        if (client_fd >= 0) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(client_fd, &read_set);
            struct timeval timeout = {
                .tv_sec = 0,
                .tv_usec = PASSPORT_TCP_POLL_MS * 1000,
            };
            int ready = select(client_fd + 1, &read_set, NULL, NULL, &timeout);
            if (ready > 0 && FD_ISSET(client_fd, &read_set)) {
                uint8_t bytes[128];
                ssize_t received = recv(client_fd, bytes, sizeof(bytes), 0);
                if (received <= 0) {
                    close_client(&client_fd);
                    passport_line_init(&line_buffer);
                } else if (passport_line_push(&line_buffer, bytes, (size_t)received,
                                               enqueue_rx, NULL) == PASSPORT_LINE_ABORTED) {
                    passport_line_init(&line_buffer);
                }
            }
            drain_tx(client_fd);
        } else {
            vTaskDelay(pdMS_TO_TICKS(PASSPORT_TCP_POLL_MS));
        }
    }

cleanup:
    close_client(&client_fd);
    if (listen_fd >= 0) close(listen_fd);
    s_connected = false;
    if (s_stopped) xSemaphoreGive(s_stopped);
    vTaskDelete(NULL);
}

static esp_err_t wifi_start(void) {
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
        return err;
    }

    wifi_config_t config = { 0 };
    memcpy(config.ap.ssid, PASSPORT_TCP_SSID, sizeof(PASSPORT_TCP_SSID) - 1);
    memcpy(config.ap.password, PASSPORT_TCP_PASSWORD, sizeof(PASSPORT_TCP_PASSWORD) - 1);
    config.ap.ssid_len = sizeof(PASSPORT_TCP_SSID) - 1;
    config.ap.channel = 1;
    config.ap.max_connection = 1;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        esp_wifi_deinit();
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
        return err;
    }
    s_wifi_started = true;
    return ESP_OK;
}

esp_err_t passport_transport_tcp_start(void) {
    if (s_started) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wifi_start();
    if (err != ESP_OK) return err;

    s_rx_queue = xQueueCreate(PASSPORT_TCP_QUEUE_DEPTH, sizeof(transport_line_t));
    s_tx_queue = xQueueCreate(PASSPORT_TCP_QUEUE_DEPTH, sizeof(transport_line_t));
    s_stopped = xSemaphoreCreateBinary();
    if (!s_rx_queue || !s_tx_queue || !s_stopped) {
        if (s_rx_queue) vQueueDelete(s_rx_queue);
        if (s_tx_queue) vQueueDelete(s_tx_queue);
        if (s_stopped) vSemaphoreDelete(s_stopped);
        s_rx_queue = NULL;
        s_tx_queue = NULL;
        s_stopped = NULL;
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();
        if (s_ap_netif) esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
        s_wifi_started = false;
        return ESP_ERR_NO_MEM;
    }
    s_stop_requested = false;
    s_connected = false;
    if (xTaskCreate(tcp_task, "passport_tcp", PASSPORT_TCP_TASK_STACK, NULL, 5, &s_task) != pdPASS) {
        vQueueDelete(s_rx_queue);
        vQueueDelete(s_tx_queue);
        vSemaphoreDelete(s_stopped);
        s_rx_queue = NULL;
        s_tx_queue = NULL;
        s_stopped = NULL;
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();
        if (s_ap_netif) esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
        s_wifi_started = false;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

esp_err_t passport_transport_tcp_stop(void) {
    if (!s_started) return ESP_OK;
    s_stop_requested = true;
    if (!s_stopped || xSemaphoreTake(s_stopped, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "TCP task stop timeout");
        return ESP_ERR_TIMEOUT;
    }
    s_task = NULL;
    if (s_rx_queue) vQueueDelete(s_rx_queue);
    if (s_tx_queue) vQueueDelete(s_tx_queue);
    if (s_stopped) vSemaphoreDelete(s_stopped);
    s_rx_queue = NULL;
    s_tx_queue = NULL;
    s_stopped = NULL;
    if (s_wifi_started) {
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();
        s_wifi_started = false;
    }
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    s_started = false;
    return ESP_OK;
}

esp_err_t passport_transport_tcp_send(const char *line) {
    if (!s_started || !line || strlen(line) >= PASSPORT_LINE_MAX) return ESP_ERR_INVALID_ARG;
    transport_line_t item = { .length = strlen(line) };
    memcpy(item.data, line, item.length);
    item.data[item.length] = '\0';
    return xQueueSend(s_tx_queue, &item, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t passport_transport_tcp_receive(char *line, size_t capacity) {
    if (!line || capacity == 0 || !s_rx_queue) return ESP_ERR_INVALID_ARG;
    transport_line_t item;
    if (xQueueReceive(s_rx_queue, &item, 0) != pdTRUE) return ESP_ERR_NOT_FOUND;
    if (item.length + 1 > capacity) return ESP_ERR_INVALID_SIZE;
    memcpy(line, item.data, item.length + 1);
    return ESP_OK;
}

bool passport_transport_tcp_connected(void) {
    return s_connected;
}
