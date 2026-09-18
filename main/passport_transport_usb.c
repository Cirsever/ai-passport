#include "passport_transport_usb.h"

#include "driver/usb_serial_jtag.h"
#include "passport_line.h"
#include "passport_usb_frame.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "passport_usb";
#define PASSPORT_USB_QUEUE_DEPTH 32
#define PASSPORT_USB_TASK_STACK 4096
#define PASSPORT_USB_POLL_MS 20
#define PASSPORT_USB_TX_BUFFER_SIZE (PASSPORT_USB_FRAME_MAX * 2)

_Static_assert(PASSPORT_USB_TX_BUFFER_SIZE > PASSPORT_USB_FRAME_MAX,
               "USB TX ring buffer must hold one complete Passport frame");

typedef struct {
    size_t length;
    char data[PASSPORT_LINE_MAX + 1];
} transport_line_t;

static QueueHandle_t s_rx_queue;
static QueueHandle_t s_tx_queue;
static SemaphoreHandle_t s_stopped;
static TaskHandle_t s_task;
static volatile bool s_stop_requested;
static bool s_started;
static bool s_driver_owned;
/* Reference count of active users. Each successful _start() bumps it; each
 * _stop() decrements and only tears the driver down when it reaches zero. */
static unsigned s_ref_count;

static bool enqueue_rx(const char *framed, size_t length, void *context) {
    (void)context;
    char line[PASSPORT_LINE_MAX + 1];
    if (length >= PASSPORT_USB_FRAME_MAX ||
        !passport_usb_frame_decode(framed, line, sizeof(line))) {
        return true;
    }
    ESP_LOGI(TAG, "RX frame decoded: %s", line);
    transport_line_t item = { .length = strlen(line) };
    memcpy(item.data, line, item.length + 1);
    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full; dropping one protocol line");
    }
    return true;
}

static void drain_tx(void) {
    transport_line_t item;
    char framed[PASSPORT_USB_FRAME_MAX];
    while (xQueueReceive(s_tx_queue, &item, 0) == pdTRUE) {
        if (!passport_usb_frame_encode(item.data, framed, sizeof(framed))) continue;
        size_t length = strlen(framed);
        framed[length++] = '\n';
        if (usb_serial_jtag_write_bytes(framed, length, pdMS_TO_TICKS(100)) <= 0) return;
    }
}

static void usb_task(void *argument) {
    (void)argument;
    passport_line_buffer_t line_buffer;
    passport_line_init(&line_buffer);

    /* USB Serial/JTAG's DTR bit is unreliable across host bridges (raw-open
     * Python bridges never assert it), so this transport does not gate RX/TX
     * on connection state. Liveness is inferred at the service layer from
     * protocol-frame idle time instead. */
    while (!s_stop_requested) {
        uint8_t bytes[128];
        int received = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), 0);
        if (received > 0) {
            ESP_LOGI(TAG, "USB RX bytes=%d", (int)received);
            (void)passport_line_push(&line_buffer, bytes, (size_t)received,
                                     enqueue_rx, NULL);
        }
        drain_tx();
        vTaskDelay(pdMS_TO_TICKS(PASSPORT_USB_POLL_MS));
    }

    if (s_stopped) xSemaphoreGive(s_stopped);
    vTaskDelete(NULL);
}

esp_err_t passport_transport_usb_start(void) {
    if (s_started) {
        s_ref_count++;
        ESP_LOGI(TAG, "USB transport shared; ref_count=%u", s_ref_count);
        return ESP_OK;
    }
    usb_serial_jtag_driver_config_t driver_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    /* The ESP-IDF default TX ring is 256 bytes, but a maximum Passport wire
     * frame is over 500 bytes. usb_serial_jtag_write_bytes() atomically queues
     * the requested item and returns 0 when the whole item does not fit, so
     * voice.capture.audio frames can never use the default buffer. */
    driver_config.tx_buffer_size = PASSPORT_USB_TX_BUFFER_SIZE;
    esp_err_t driver_err = usb_serial_jtag_driver_install(&driver_config);
    if (driver_err != ESP_OK) {
        ESP_LOGE(TAG, "USB Serial/JTAG driver install failed: %s",
                 esp_err_to_name(driver_err));
        return driver_err;
    }
    s_driver_owned = true;
    s_rx_queue = xQueueCreate(PASSPORT_USB_QUEUE_DEPTH, sizeof(transport_line_t));
    s_tx_queue = xQueueCreate(PASSPORT_USB_QUEUE_DEPTH, sizeof(transport_line_t));
    s_stopped = xSemaphoreCreateBinary();
    if (!s_rx_queue || !s_tx_queue || !s_stopped) {
        if (s_rx_queue) vQueueDelete(s_rx_queue);
        if (s_tx_queue) vQueueDelete(s_tx_queue);
        if (s_stopped) vSemaphoreDelete(s_stopped);
        s_rx_queue = NULL;
        s_tx_queue = NULL;
        s_stopped = NULL;
        (void)usb_serial_jtag_driver_uninstall();
        s_driver_owned = false;
        return ESP_ERR_NO_MEM;
    }
    s_stop_requested = false;
    if (xTaskCreate(usb_task, "passport_usb", PASSPORT_USB_TASK_STACK, NULL, 5,
                    &s_task) != pdPASS) {
        vQueueDelete(s_rx_queue);
        vQueueDelete(s_tx_queue);
        vSemaphoreDelete(s_stopped);
        s_rx_queue = NULL;
        s_tx_queue = NULL;
        s_stopped = NULL;
        (void)usb_serial_jtag_driver_uninstall();
        s_driver_owned = false;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    s_ref_count = 1;
    ESP_LOGI(TAG, "USB Serial/JTAG transport ready; protocol prefix=%s",
             PASSPORT_USB_FRAME_PREFIX);
    return ESP_OK;
}

esp_err_t passport_transport_usb_stop(void) {
    if (!s_started) return ESP_OK;
    if (s_ref_count > 1) {
        s_ref_count--;
        ESP_LOGI(TAG, "USB transport shared; ref_count=%u", s_ref_count);
        return ESP_OK;
    }
    s_stop_requested = true;
    if (!s_stopped || xSemaphoreTake(s_stopped, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "USB task stop timeout");
        return ESP_ERR_TIMEOUT;
    }
    s_task = NULL;
    vQueueDelete(s_rx_queue);
    vQueueDelete(s_tx_queue);
    vSemaphoreDelete(s_stopped);
    s_rx_queue = NULL;
    s_tx_queue = NULL;
    s_stopped = NULL;
    s_started = false;
    s_ref_count = 0;
    if (s_driver_owned) {
        (void)usb_serial_jtag_driver_uninstall();
        s_driver_owned = false;
    }
    return ESP_OK;
}

esp_err_t passport_transport_usb_send(const char *line) {
    if (!s_started || !line || strlen(line) >= PASSPORT_LINE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    transport_line_t item = { .length = strlen(line) };
    memcpy(item.data, line, item.length + 1);
    return xQueueSend(s_tx_queue, &item, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t passport_transport_usb_receive(char *line, size_t capacity) {
    if (!line || capacity == 0 || !s_rx_queue) return ESP_ERR_INVALID_ARG;
    transport_line_t item;
    if (xQueueReceive(s_rx_queue, &item, 0) != pdTRUE) return ESP_ERR_NOT_FOUND;
    if (item.length + 1 > capacity) return ESP_ERR_INVALID_SIZE;
    memcpy(line, item.data, item.length + 1);
    return ESP_OK;
}
