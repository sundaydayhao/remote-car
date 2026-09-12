#include "rc_audio.h"
#include "rc_codec.h"
#include "rc_network.h"
#include "rc_protocol.h"
#include "esp_aec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

typedef struct { uint32_t ip, session, generation; bool enabled; } peer_t;
typedef struct { uint32_t generation, arrived; int16_t pcm[RC_AUDIO_SAMPLES]; } audio_frame_t;
static peer_t peer;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t playback_queue;
static aec_handle_t *aec;
static size_t aec_samples;
static int udp = -1;
static rc_audio_stats_t stats;
static const char *TAG = "duplex_aec";
static peer_t get_peer(void) {
    portENTER_CRITICAL(&lock); peer_t p = peer; portEXIT_CRITICAL(&lock); return p;
}
rc_audio_stats_t rc_audio_stats(void) {
    portENTER_CRITICAL(&lock); rc_audio_stats_t s = stats; portEXIT_CRITICAL(&lock); return s;
}
void rc_audio_peer(uint32_t ip, uint32_t session, bool enabled) {
    portENTER_CRITICAL(&lock);
    if (peer.ip != ip || peer.session != session || peer.enabled != enabled) ++peer.generation;
    peer.ip = ip; peer.session = session; peer.enabled = enabled && ip && session;
    portEXIT_CRITICAL(&lock);
}
static void *buffer(size_t n) {
    void *p = heap_caps_aligned_calloc(16, 1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(p); return p;
}
static void capture_task(void *arg) {
    (void)arg;
    int16_t *stereo = buffer(aec_samples * 4), *mic = buffer(aec_samples * 2);
    int16_t *ref = buffer(aec_samples * 2), *clean = buffer(aec_samples * 2);
    rc_audio_packer_t *packer = buffer(sizeof(*packer));
    uint8_t *packet = buffer(RC_AUDIO_BYTES);
    uint32_t sequence = 0, generation = ~0u;
    for (;;) {
        if (rc_codec_read(stereo, aec_samples) != ESP_OK) {
            packer->used = 0;
            portENTER_CRITICAL(&lock); ++stats.io_errors; portEXIT_CRITICAL(&lock);
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        uint32_t mp = 0, rp = 0;
        for (size_t i = 0; i < aec_samples; ++i) {
            mic[i] = stereo[i * 2]; ref[i] = stereo[i * 2 + 1];
            if ((unsigned)abs((int)mic[i]) > mp) mp = abs((int)mic[i]);
            if ((unsigned)abs((int)ref[i]) > rp) rp = abs((int)ref[i]);
        }
        // Both boards supply a sample-synchronous playback reference in the right input slot.
        // This always runs during playback AND near-end speech. No PTT, VAD gate, or raw bypass.
        aec_process(aec, mic, ref, clean);
        rc_limit_pcm(clean, aec_samples);
        portENTER_CRITICAL(&lock); stats.mic_peak = mp; stats.ref_peak = rp; portEXIT_CRITICAL(&lock);
        peer_t p = get_peer();
        if (generation != p.generation) { generation = p.generation; packer->used = 0; }
        if (!p.enabled) { packer->used = 0; continue; }
        // AEC frame size is mode-dependent; UDP always carries 512 samples.
        size_t offset = 0;
        while (offset < aec_samples) {
            offset += rc_audio_pack(packer, clean + offset, aec_samples - offset);
            if (packer->used != RC_AUDIO_SAMPLES) continue;
            rc_audio_encode(packet, p.session, ++sequence, packer->pcm);
            packer->used = 0;
            struct sockaddr_in dst = {.sin_family = AF_INET, .sin_port = htons(RC_AUDIO_PORT), .sin_addr.s_addr = p.ip};
            int n = sendto(udp, packet, RC_AUDIO_BYTES, 0, (void *)&dst, sizeof(dst));
            portENTER_CRITICAL(&lock);
            if (n == RC_AUDIO_BYTES) ++stats.sent; else ++stats.dropped;
            portEXIT_CRITICAL(&lock);
        }
    }
}
static void receive_task(void *arg) {
    (void)arg;
    uint8_t *packet = buffer(RC_AUDIO_BYTES + 1);
    audio_frame_t *frame = buffer(sizeof(*frame)), *old = buffer(sizeof(*old));
    uint32_t generation = ~0u, last = 0;
    bool have_seq = false;
    for (;;) {
        struct sockaddr_in src; socklen_t len = sizeof(src);
        int n = recvfrom(udp, packet, RC_AUDIO_BYTES + 1, 0, (void *)&src, &len);
        if (n < 0) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }
        peer_t p = get_peer();
        if (generation != p.generation) { generation = p.generation; have_seq = false; }
        uint32_t seq;
        if (!p.enabled || src.sin_addr.s_addr != p.ip || src.sin_port != htons(RC_AUDIO_PORT) ||
            !rc_audio_decode(packet, n, p.session, &seq, frame->pcm) || (have_seq && !rc_seq_newer(seq, last))) {
            portENTER_CRITICAL(&lock); ++stats.dropped; portEXIT_CRITICAL(&lock); continue;
        }
        last = seq; have_seq = true; frame->generation = generation; frame->arrived = rc_now_ms();
        if (xQueueSend(playback_queue, frame, 0) != pdTRUE) {
            xQueueReceive(playback_queue, old, 0); xQueueSend(playback_queue, frame, 0);
            portENTER_CRITICAL(&lock); ++stats.dropped; portEXIT_CRITICAL(&lock);
        }
        portENTER_CRITICAL(&lock); ++stats.received; portEXIT_CRITICAL(&lock);
    }
}
static void playback_task(void *arg) {
    (void)arg;
    audio_frame_t *frame = buffer(sizeof(*frame)); int16_t *stereo = buffer(RC_AUDIO_SAMPLES * 4);
    int32_t previous = 0;
    uint32_t generation = ~0u;
    bool primed = false;
    for (;;) {
        peer_t p = get_peer();
        if (generation != p.generation) { generation = p.generation; primed = false; previous = 0; }
        if (!p.enabled) {
            while (xQueueReceive(playback_queue, frame, 0) == pdTRUE) {}
            primed = false;
        }
        if (p.enabled && uxQueueMessagesWaiting(playback_queue) >= 2) primed = true;
        bool good = primed && xQueueReceive(playback_queue, frame, 0) == pdTRUE && p.enabled &&
                    frame->generation == p.generation && rc_now_ms() - frame->arrived <= 128;
        if (!good) primed = false;
        if (!good && p.enabled) { portENTER_CRITICAL(&lock); ++stats.underruns; portEXIT_CRITICAL(&lock); }
        for (unsigned i = 0; i < RC_AUDIO_SAMPLES; ++i) {
            int32_t sample = good ? frame->pcm[i] : 0;
            // Short ramp at discontinuities; fixed ceiling avoids hard DAC clipping.
            if (sample > 24000) sample = 24000;
            if (sample < -24000) sample = -24000;
            if (i < 32) sample = (previous * (31 - i) + sample * (i + 1)) / 32;
            stereo[i * 2] = stereo[i * 2 + 1] = sample;
        }
        previous = good ? frame->pcm[RC_AUDIO_SAMPLES - 1] : 0;
        // Silence is also written to keep the clock/reference continuous and discard old DMA data.
        if (rc_codec_write(stereo, RC_AUDIO_SAMPLES) != ESP_OK) {
            portENTER_CRITICAL(&lock); ++stats.io_errors; portEXIT_CRITICAL(&lock);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
esp_err_t rc_audio_start(void) {
    aec = aec_create(16000, 4, 1, AEC_MODE_VOIP_HIGH_PERF);
    if (!aec) return ESP_ERR_NO_MEM;
    int chunk = aec_get_chunksize(aec);
    ESP_LOGI(TAG, "AEC frame: %d samples; UDP packet: %d samples", chunk, RC_AUDIO_SAMPLES);
    // Keep one capture below the codec's 100 ms read deadline.
    if (chunk <= 0 || chunk > 1024) {
        ESP_LOGE(TAG, "Unsupported AEC frame length: %d", chunk);
        aec_destroy(aec); aec = NULL; return ESP_ERR_NOT_SUPPORTED;
    }
    aec_samples = (size_t)chunk;
    playback_queue = xQueueCreate(3, sizeof(audio_frame_t));
    if (!playback_queue) return ESP_ERR_NO_MEM;
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(RC_AUDIO_PORT), .sin_addr.s_addr = INADDR_ANY};
    if (udp < 0 || bind(udp, (void *)&local, sizeof(local)) != 0) return ESP_FAIL;
    fcntl(udp, F_SETFL, O_NONBLOCK);
    if (xTaskCreate(playback_task, "audio_play", 4096, NULL, 8, NULL) != pdPASS ||
        xTaskCreate(receive_task, "audio_rx", 4096, NULL, 7, NULL) != pdPASS ||
        xTaskCreate(capture_task, "audio_aec", 8192, NULL, 7, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&lock); stats.ready = true; portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Full duplex: 16 kHz, %u-sample AEC, 512-sample UDP, hardware reference", (unsigned)aec_samples);
    return ESP_OK;
}
