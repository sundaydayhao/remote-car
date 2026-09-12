#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RC_CONTROL_PORT 5000
#define RC_AUDIO_PORT 5001
#define RC_CONTROL_BYTES 24
#define RC_AUDIO_SAMPLES 512
#define RC_AUDIO_HEADER 20
#define RC_AUDIO_BYTES (RC_AUDIO_HEADER + RC_AUDIO_SAMPLES * 2)
#define RC_DEADMAN_MS 350
#define RC_INPUT_MS 180
#define RC_AUDIO_ON 1
#define RC_MOTOR_OK 2
#define RC_AUDIO_OK 4

typedef struct {
    uint32_t session;
    uint32_t seq;
    int16_t left, right;
    uint8_t flags;
} rc_command_t;
typedef struct { uint32_t session, seq, last_ms; bool first; } rc_guard_t;
void rc_guard_init(rc_guard_t *guard, uint32_t session, uint32_t now);
bool rc_guard_accept(rc_guard_t *guard, const rc_command_t *command, uint32_t now);
bool rc_command_expired(uint32_t now, uint32_t last_ms);
int rc_motor_ramp(int from, int to);

uint32_t rc_read32(const uint8_t *p);
void rc_write32(uint8_t *p, uint32_t v);
uint32_t rc_crc32(const void *data, size_t len);
bool rc_seq_newer(uint32_t a, uint32_t b);
void rc_control_encode(uint8_t out[RC_CONTROL_BYTES], const rc_command_t *v);
bool rc_control_decode(const uint8_t *p, size_t len, rc_command_t *v);
void rc_audio_encode(uint8_t out[RC_AUDIO_BYTES], uint32_t session, uint32_t seq, const int16_t *pcm);
bool rc_audio_decode(const uint8_t *p, size_t len, uint32_t session, uint32_t *seq, int16_t *pcm);
void rc_mix(unsigned direction, unsigned speed, int16_t *left, int16_t *right);
void rc_limit_pcm(int16_t *pcm, size_t samples);
