#include "passport_voice_worker.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "passport_service.h"
#include "passport_transport_usb.h"
#include "passport_voice_vad.h"

static const char *TAG = "passport_voice";

// One 20 ms VAD block at 16 kHz mono s16 = 640 bytes PCM.
#define VOICE_BLOCK_MS         20
#define VOICE_SAMPLE_RATE      16000
#define VOICE_BLOCK_SAMPLES    ((VOICE_SAMPLE_RATE * VOICE_BLOCK_MS) / 1000)
#define VOICE_BLOCK_BYTES      (VOICE_BLOCK_SAMPLES * 2)
// wire frame is 512 chars max; base64(N) = ceil(N/3)*4. Split each 20 ms
// block into two 10 ms audio frames so payload + JSON envelope fit.
#define VOICE_CHUNK_BYTES      240
#define VOICE_CHUNK_B64_MAX    324

// Notification bits sent to the worker task.
#define NOTIFY_BEGIN  (1u << 0)
#define NOTIFY_STOP   (1u << 1)
#define NOTIFY_EXIT   (1u << 2)

static TaskHandle_t     s_task;
static SemaphoreHandle_t s_state_mtx;
static passport_voice_status_t s_status;
static uint32_t         s_request_seq;
static volatile bool    s_active;
static char             s_route[96];

static const char b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *src, size_t n, char *dst) {
    size_t i = 0, o = 0;
    while (i + 3 <= n) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) |
                     (uint32_t)src[i + 2];
        dst[o++] = b64_alphabet[(v >> 18) & 0x3F];
        dst[o++] = b64_alphabet[(v >> 12) & 0x3F];
        dst[o++] = b64_alphabet[(v >> 6) & 0x3F];
        dst[o++] = b64_alphabet[v & 0x3F];
        i += 3;
    }
    if (i < n) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < n) v |= (uint32_t)src[i + 1] << 8;
        dst[o++] = b64_alphabet[(v >> 18) & 0x3F];
        dst[o++] = b64_alphabet[(v >> 12) & 0x3F];
        dst[o++] = (i + 1 < n) ? b64_alphabet[(v >> 6) & 0x3F] : '=';
        dst[o++] = '=';
    }
    dst[o] = '\0';
    return o;
}

static void publish_status_locked(bool active, bool completed,
                                  uint16_t elapsed_ms,
                                  uint8_t level, uint16_t chunks_sent,
                                  uint16_t max_ms) {
    if (!s_state_mtx) return;
    if (xSemaphoreTake(s_state_mtx, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_status.active = active;
        s_status.completed = completed;
        s_status.elapsed_ms = elapsed_ms;
        s_status.level = level;
        s_status.chunks_sent = chunks_sent;
        s_status.max_ms = max_ms;
        xSemaphoreGive(s_state_mtx);
    }
}

static void send_with_retry(const char *line, int max_attempts) {
    // The transport TX queue is shared with other Passport
    // frames (task.state, approval.*, ...). audio chunks arrive every 10 ms
    // — far faster than the consumer can drain — so plain fire-and-forget
    // send() silently drops most of them. Retry a few ticks so short bursts
    // (start / stop / low chunk rate) reach the wire while sustained
    // over-run still degrades gracefully (chunks are lossy by design; VAD
    // still stops on silence).
    for (int i = 0; i < max_attempts; ++i) {
        if (passport_transport_usb_send(line) == ESP_OK) return;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void emit_start(uint32_t seq) {
    char line[PASSPORT_SERVICE_LINE_MAX];
    int n = snprintf(line, sizeof(line),
                     "{\"type\":\"voice.capture.start\","
                     "\"request_id\":\"v-%u\","
                     "\"sample_rate\":%d,\"codec\":\"pcm16\"%s}",
                     (unsigned)seq, VOICE_SAMPLE_RATE, s_route);
    if (n > 0 && (size_t)n < sizeof(line)) send_with_retry(line, 20);
}

static void emit_chunk(uint32_t seq, uint16_t index,
                       const uint8_t *pcm, size_t bytes) {
    char b64[VOICE_CHUNK_B64_MAX + 4];
    size_t b64_len = base64_encode(pcm, bytes, b64);
    (void)b64_len;
    char line[PASSPORT_SERVICE_LINE_MAX];
    int n = snprintf(line, sizeof(line),
                     "{\"type\":\"voice.capture.audio\","
                     "\"request_id\":\"v-%u\","
                     "\"index\":%u,\"pcm_b64\":\"%s\"%s}",
                     (unsigned)seq, (unsigned)index, b64, s_route);
    if (n > 0 && (size_t)n < sizeof(line)) {
        // Audio chunks are the flood; a single retry is enough to smooth
        // 8-deep queue jitter while lossy backpressure remains the design.
        (void)passport_transport_usb_send(line);
    }
}

static const char *reason_string(uint16_t stop_reason) {
    switch (stop_reason) {
    case PASSPORT_VOICE_STOP_SILENCE: return "silence";
    case PASSPORT_VOICE_STOP_MAX_DURATION: return "max_duration";
    case PASSPORT_VOICE_STOP_MANUAL: return "manual";
    default: return "unknown";
    }
}

static void emit_stop(uint32_t seq, uint32_t duration_ms, uint16_t chunks,
                      uint8_t peak_level, uint16_t stop_reason) {
    // Diagnostic placeholder text so Codex / Trae adapters have something
    // to forward until STT lands. Bounded and self-describing.
    char line[PASSPORT_SERVICE_LINE_MAX];
    int n = snprintf(line, sizeof(line),
                     "{\"type\":\"voice.capture.stop\","
                     "\"request_id\":\"v-%u\","
                     "\"duration_ms\":%u,"
                     "\"reason\":\"%s\","
                     "\"text\":\"[voice %u chunks %ums peak=%u]\"%s}",
                     (unsigned)seq, (unsigned)duration_ms,
                     reason_string(stop_reason),
                     (unsigned)chunks, (unsigned)duration_ms,
                     (unsigned)peak_level, s_route);
    if (n > 0 && (size_t)n < sizeof(line)) send_with_retry(line, 40);
}

static void run_one_utterance(void) {
    // Configure codec to 16 kHz mono s16. Safe to call every utterance;
    // bsp_audio_set_format is idempotent for equal formats.
    esp_err_t err = bsp_audio_set_format(VOICE_SAMPLE_RATE, 16, 1);
    if (err != ESP_OK) {
        s_active = false;
        publish_status_locked(false, false, 0, 0, 0, 0);
        ESP_LOGE(TAG, "audio format setup failed: %s", esp_err_to_name(err));
        return;
    }
    uint32_t seq = ++s_request_seq;
    passport_voice_vad_t vad;
    passport_voice_vad_init(&vad);
    // Match wire format to VAD block size (20 ms).
    int16_t block[VOICE_BLOCK_SAMPLES];
    uint16_t chunks_sent = 0;
    uint8_t  peak_level = 0;
    publish_status_locked(true, false, 0, 0, 0, vad.max_utterance_ms);
    emit_start(seq);

    uint32_t consecutive_read_fails = 0;
    while (!vad.stopped) {
        // Poll manual stop notification without blocking. Bit-only wait.
        uint32_t bits = 0;
        (void)xTaskNotifyWait(0, NOTIFY_STOP | NOTIFY_EXIT, &bits, 0);
        if (bits & NOTIFY_EXIT) {
            passport_voice_vad_stop(&vad);
            break;
        }
        if (bits & NOTIFY_STOP) passport_voice_vad_stop(&vad);
        if (vad.stopped) break;

        esp_err_t rd = bsp_audio_read(block, VOICE_BLOCK_BYTES);
        if (rd != ESP_OK) {
            ESP_LOGW(TAG, "bsp_audio_read failed (%s); aborting",
                     esp_err_to_name(rd));
            if (++consecutive_read_fails >= 3) {
                passport_voice_vad_stop(&vad);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        consecutive_read_fails = 0;
        passport_voice_vad_feed(&vad, block, VOICE_BLOCK_SAMPLES);
        if (vad.level > peak_level) peak_level = vad.level;

        for (size_t offset = 0; offset < VOICE_BLOCK_BYTES;
             offset += VOICE_CHUNK_BYTES) {
            size_t bytes = VOICE_BLOCK_BYTES - offset;
            if (bytes > VOICE_CHUNK_BYTES) bytes = VOICE_CHUNK_BYTES;
            emit_chunk(seq, chunks_sent++, (const uint8_t *)block + offset,
                       bytes);
        }

        publish_status_locked(true, false, (uint16_t)vad.utterance_ms,
                              vad.level, chunks_sent, vad.max_utterance_ms);
    }

    emit_stop(seq, vad.utterance_ms, chunks_sent, peak_level,
              vad.stop_reason);
    s_active = false;
    publish_status_locked(false, true, 0, 0, 0, vad.max_utterance_ms);
}

static void voice_task(void *arg) {
    (void)arg;
    for (;;) {
        uint32_t bits = 0;
        // Wait indefinitely for begin/exit. STOP arriving with no active
        // utterance is dropped by the notification API (bits cleared here).
        if (xTaskNotifyWait(0, NOTIFY_BEGIN | NOTIFY_EXIT, &bits,
                            portMAX_DELAY) != pdTRUE) continue;
        if (bits & NOTIFY_EXIT) break;
        if (bits & NOTIFY_BEGIN) run_one_utterance();
    }
    // Signal completion by clearing the handle; the stopper spins on this.
    s_active = false;
    publish_status_locked(false, false, 0, 0, 0, 0);
    TaskHandle_t self = s_task;
    s_task = NULL;
    (void)self;
    vTaskDelete(NULL);
}

esp_err_t passport_voice_worker_start(void) {
    if (s_task) return ESP_OK;
    if (!s_state_mtx) {
        s_state_mtx = xSemaphoreCreateMutex();
        if (!s_state_mtx) return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    BaseType_t ok = xTaskCreate(voice_task, "passport_voice", 4096, NULL,
                                tskIDLE_PRIORITY + 3, &s_task);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t passport_voice_worker_begin(void) {
    if (!s_task) return ESP_ERR_INVALID_STATE;
    if (s_active) return ESP_OK;   // one utterance at a time
    /* Claim the capture before notifying the task so a release arriving
     * immediately after the long-press cannot race ahead of run_one_utterance
     * and get dropped as an inactive stop. */
    s_active = true;
    if (xTaskNotify(s_task, NOTIFY_BEGIN, eSetBits) != pdPASS) {
        s_active = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void passport_voice_worker_set_route(const char *bridge, const char *sid,
                                     uint32_t epoch) {
    if (!bridge || !sid || !bridge[0] || !sid[0]) {
        s_route[0] = '\0';
        return;
    }
    int written = snprintf(s_route, sizeof(s_route),
                           ",\"bridge\":\"%s\",\"sid\":\"%s\",\"epoch\":%lu",
                           bridge, sid, (unsigned long)epoch);
    if (written <= 0 || (size_t)written >= sizeof(s_route)) {
        s_route[0] = '\0';
    }
}

esp_err_t passport_voice_worker_manual_stop(void) {
    if (!s_task || !s_active) return ESP_OK;
    xTaskNotify(s_task, NOTIFY_STOP, eSetBits);
    return ESP_OK;
}

void passport_voice_worker_snapshot(passport_voice_status_t *out) {
    if (!out) return;
    if (!s_state_mtx) { memset(out, 0, sizeof(*out)); return; }
    if (xSemaphoreTake(s_state_mtx, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = s_status;
        xSemaphoreGive(s_state_mtx);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

esp_err_t passport_voice_worker_stop(void) {
    if (!s_task) return ESP_OK;
    xTaskNotify(s_task, NOTIFY_EXIT | NOTIFY_STOP, eSetBits);
    // Wait up to 1 s for the task to exit cleanly.
    for (int i = 0; i < 100 && s_task; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}
