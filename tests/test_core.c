#include "rc_protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
int main(void) {
    CHECK(rc_crc32("123456789", 9) == 0xcbf43926);
    CHECK(rc_seq_newer(1, 0)); CHECK(!rc_seq_newer(0, 0));
    CHECK(!rc_seq_newer(4, 5)); CHECK(rc_seq_newer(0, 0xffffffff));
    CHECK(!rc_seq_newer(0x80000000, 0));
    uint8_t wire[RC_AUDIO_BYTES + 1];
    rc_command_t c = {.session = 0x78563412, .seq = 99, .left = -100, .right = 100, .flags = RC_AUDIO_ON}, out;
    rc_control_encode(wire, &c);
    CHECK(wire[4] == 0x12 && wire[7] == 0x78 && wire[12] == 0x9c && wire[13] == 0xff);
    CHECK(rc_control_decode(wire, RC_CONTROL_BYTES, &out)); CHECK(out.left == -100 && out.right == 100 && out.seq == 99);
    for (unsigned i = 0; i < RC_CONTROL_BYTES; ++i) CHECK(!rc_control_decode(wire, i, &out));
    CHECK(!rc_control_decode(wire, RC_CONTROL_BYTES + 1, &out));
    for (unsigned i = 0; i < RC_CONTROL_BYTES; ++i) {
        wire[i] ^= 1; CHECK(!rc_control_decode(wire, RC_CONTROL_BYTES, &out)); wire[i] ^= 1;
    }
    c.left = 101; rc_control_encode(wire, &c); CHECK(!rc_control_decode(wire, RC_CONTROL_BYTES, &out));
    c.left = -101; rc_control_encode(wire, &c); CHECK(!rc_control_decode(wire, RC_CONTROL_BYTES, &out));
    c.left = 0; c.flags = 8; rc_control_encode(wire, &c); CHECK(!rc_control_decode(wire, RC_CONTROL_BYTES, &out));
    rc_guard_t g; rc_guard_init(&g, 7, 0);
    c = (rc_command_t){.session = 7, .seq = 1, .left = 10};
    CHECK(!rc_guard_accept(&g, &c, 10)); // first command must stop
    c.left = 0; CHECK(rc_guard_accept(&g, &c, 10));
    CHECK(!rc_guard_accept(&g, &c, 50)); CHECK(g.last_ms == 10); // replay does not refresh watchdog
    c.seq = 2; c.left = 60; CHECK(rc_guard_accept(&g, &c, 60));
    c.seq = 3; c.session = 8; CHECK(!rc_guard_accept(&g, &c, 70));
    c.session = 7; CHECK(!rc_guard_accept(&g, &c, 410)); CHECK(g.last_ms == 60);
    rc_guard_init(&g, 7, 0xfffffff0); c.seq = 1; c.left = 0;
    CHECK(rc_guard_accept(&g, &c, 10));
    CHECK(!rc_command_expired(100, 0xfffffff0)); CHECK(rc_command_expired(400, 0xfffffff0));
    CHECK(!rc_command_expired(349, 0)); CHECK(rc_command_expired(350, 0));
    CHECK(rc_motor_ramp(10, -100) == 0); CHECK(rc_motor_ramp(0, -100) == -10);
    CHECK(rc_motor_ramp(25, -20) == 15); CHECK(rc_motor_ramp(-8, 20) == 0);
    int16_t l, r;
    rc_mix(1, 40, &l, &r); CHECK(l == 40 && r == 40);
    rc_mix(2, 40, &l, &r); CHECK(l == -40 && r == -40);
    rc_mix(3, 40, &l, &r); CHECK(l == -40 && r == 40);
    rc_mix(4, 40, &l, &r); CHECK(l == 40 && r == -40);
    rc_mix(0, 100, &l, &r); CHECK(!l && !r);
    rc_mix(99, 100, &l, &r); CHECK(!l && !r);
    rc_mix(1, 1000, &l, &r); CHECK(l == 100 && r == 100);
    int16_t pcm[RC_AUDIO_SAMPLES], decoded[RC_AUDIO_SAMPLES]; uint32_t seq;
    for (unsigned i = 0; i < RC_AUDIO_SAMPLES; ++i) pcm[i] = (int16_t)(i * 131 - 32768);
    rc_audio_encode(wire, 5, 123, pcm);
    CHECK(rc_audio_decode(wire, RC_AUDIO_BYTES, 5, &seq, decoded));
    CHECK(seq == 123 && memcmp(pcm, decoded, sizeof(pcm)) == 0);
    CHECK(!rc_audio_decode(wire, RC_AUDIO_BYTES, 6, &seq, decoded));
    CHECK(!rc_audio_decode(wire, RC_AUDIO_BYTES, 0, &seq, decoded));
    CHECK(!rc_audio_decode(wire, RC_AUDIO_BYTES + 1, 5, &seq, decoded));
    for (unsigned i = 0; i < RC_AUDIO_BYTES; ++i) {
        CHECK(!rc_audio_decode(wire, i, 5, &seq, decoded));
        wire[i] ^= 1; CHECK(!rc_audio_decode(wire, RC_AUDIO_BYTES, 5, &seq, decoded)); wire[i] ^= 1;
    }
    int16_t limits[] = {-32768, -24001, -24000, -1, 0, 1, 24000, 24001, 32767};
    rc_limit_pcm(limits, 9);
    CHECK(limits[0] == -24000 && limits[1] == -24000 && limits[3] == -1 && limits[5] == 1 && limits[8] == 24000);
    printf("PASS: %u production-C checks (protocol, CRC, sessions, replay, watchdog, reversing, PCM)\n", checks);
    return 0;
}
