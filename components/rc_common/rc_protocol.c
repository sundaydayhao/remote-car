#include "rc_protocol.h"
#include <string.h>

uint32_t rc_read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
void rc_write32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
uint32_t rc_crc32(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t crc = ~0u;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
bool rc_seq_newer(uint32_t a, uint32_t b) { return a != b && (a - b) < 0x80000000u; }
bool rc_command_expired(uint32_t now, uint32_t last_ms) { return now - last_ms >= RC_DEADMAN_MS; }
void rc_guard_init(rc_guard_t *g, uint32_t session, uint32_t now) {
    g->session = session; g->seq = 0; g->last_ms = now; g->first = true;
}
bool rc_guard_accept(rc_guard_t *g, const rc_command_t *v, uint32_t now) {
    if (!g->session || v->session != g->session || !rc_seq_newer(v->seq, g->seq) ||
        (v->flags & ~RC_AUDIO_ON) || (g->first && (v->left || v->right)) ||
        v->left < -100 || v->left > 100 || v->right < -100 || v->right > 100 ||
        rc_command_expired(now, g->last_ms)) return false;
    g->seq = v->seq; g->last_ms = now; g->first = false; return true;
}
int rc_motor_ramp(int from, int to) {
    if ((from > 0 && to < 0) || (from < 0 && to > 0)) to = 0;
    if (to > from + 10) return from + 10;
    if (to < from - 10) return from - 10;
    return to;
}
void rc_control_encode(uint8_t out[RC_CONTROL_BYTES], const rc_command_t *v) {
    memset(out, 0, RC_CONTROL_BYTES);
    memcpy(out, "RC01", 4);
    rc_write32(out + 4, v->session);
    rc_write32(out + 8, v->seq);
    out[12] = (uint8_t)v->left;
    out[13] = (uint8_t)(v->left >> 8);
    out[14] = (uint8_t)v->right;
    out[15] = (uint8_t)(v->right >> 8);
    out[16] = v->flags;
    rc_write32(out + 20, rc_crc32(out, 20));
}
bool rc_control_decode(const uint8_t *p, size_t len, rc_command_t *v) {
    if (len != RC_CONTROL_BYTES || memcmp(p, "RC01", 4) ||
        rc_read32(p + 20) != rc_crc32(p, 20) || p[17] || p[18] || p[19] || (p[16] & ~7u)) return false;
    v->session = rc_read32(p + 4); v->seq = rc_read32(p + 8);
    v->left = (int16_t)(p[12] | (uint16_t)p[13] << 8);
    v->right = (int16_t)(p[14] | (uint16_t)p[15] << 8);
    v->flags = p[16];
    return v->left >= -100 && v->left <= 100 && v->right >= -100 && v->right <= 100;
}
void rc_audio_encode(uint8_t out[RC_AUDIO_BYTES], uint32_t session, uint32_t seq, const int16_t *pcm) {
    memcpy(out, "AU01", 4); rc_write32(out + 4, session); rc_write32(out + 8, seq);
    rc_write32(out + 12, RC_AUDIO_SAMPLES);
    for (int i = 0; i < RC_AUDIO_SAMPLES; ++i) {
        out[RC_AUDIO_HEADER + i * 2] = (uint8_t)pcm[i];
        out[RC_AUDIO_HEADER + i * 2 + 1] = (uint8_t)((uint16_t)pcm[i] >> 8);
    }
    rc_write32(out + 16, rc_crc32(out + RC_AUDIO_HEADER, RC_AUDIO_SAMPLES * 2) ^ rc_crc32(out, 16));
}
bool rc_audio_decode(const uint8_t *p, size_t len, uint32_t session, uint32_t *seq, int16_t *pcm) {
    if (len != RC_AUDIO_BYTES || memcmp(p, "AU01", 4) || !session || rc_read32(p + 4) != session ||
        rc_read32(p + 12) != RC_AUDIO_SAMPLES || rc_read32(p + 16) !=
        (rc_crc32(p + RC_AUDIO_HEADER, RC_AUDIO_SAMPLES * 2) ^ rc_crc32(p, 16))) return false;
    *seq = rc_read32(p + 8);
    for (int i = 0; i < RC_AUDIO_SAMPLES; ++i)
        pcm[i] = (int16_t)(p[RC_AUDIO_HEADER + i * 2] | (uint16_t)p[RC_AUDIO_HEADER + i * 2 + 1] << 8);
    return true;
}
void rc_mix(unsigned direction, unsigned speed, int16_t *left, int16_t *right) {
    int s = speed > 100 ? 100 : (int)speed;
    *left = *right = 0;
    switch (direction) {
        case 1: *left = *right = s; break;
        case 2: *left = *right = -s; break;
        case 3: *left = -s; *right = s; break;
        case 4: *left = s; *right = -s; break;
        default: break;
    }
}
void rc_limit_pcm(int16_t *pcm, size_t samples) {
    // Fixed headroom. No automatic gain boost: it would increase loop gain in quiet rooms.
    for (size_t i = 0; i < samples; ++i) {
        int v = pcm[i];
        if (v > 24000) v = 24000;
        if (v < -24000) v = -24000;
        pcm[i] = (int16_t)v;
    }
}
