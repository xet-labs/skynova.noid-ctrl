/*
 * test_wire.c - tests for the wire library (CRC, COBS, frames, parser).
 *
 * Run on the PC:  pio test -e native
 *
 * The main idea: every row in spec/vectors.json (turned into vectors.h) must
 * be reproduced exactly, in both directions. If this passes, the ESP32 and the
 * Python side speak the same bytes. The remaining tests check that damaged
 * frames are rejected and that the parser copes with text and junk.
 */
#include <string.h>
#include <unity.h>
#include "skynoid_wire.h"
#include "vectors.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- CRC ---------------------------------------------------------------- */
static void test_crc16_check_value(void)
{
    const uint8_t text[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(CRC16_CHECK_VALUE, skw_crc16(text, 9));
}

/* ---- COBS --------------------------------------------------------------- */
static void test_cobs_vectors_encode_and_decode(void)
{
    uint8_t enc[32], dec[32];
    for (size_t i = 0; i < COBS_VECTOR_COUNT; i++) {
        const cobs_vec_t *v = &COBS_VECTORS[i];
        size_t n = skw_cobs_encode(v->in, v->in_len, enc);
        TEST_ASSERT_EQUAL_UINT32(v->out_len, n);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(v->out, enc, n);

        int m = skw_cobs_decode(v->out, v->out_len, dec, sizeof dec);
        TEST_ASSERT_EQUAL_INT((int)v->in_len, m);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(v->in, dec, v->in_len);
    }
}

static void test_cobs_decode_rejects_zero_inside(void)
{
    const uint8_t bad[] = { 0x03, 0x11, 0x00, 0x22 };   /* 0x00 can never be inside */
    uint8_t out[8];
    TEST_ASSERT_EQUAL_INT(-1, skw_cobs_decode(bad, sizeof bad, out, sizeof out));
}

/* ---- whole frames ------------------------------------------------------- */
static void test_frame_encode_matches_vectors(void)
{
    uint8_t wire[SKW_MAX_WIRE];
    for (size_t i = 0; i < FRAME_VECTOR_COUNT; i++) {
        const frame_vec_t *v = &FRAME_VECTORS[i];
        skw_frame_t f;
        memset(&f, 0, sizeof f);
        f.type = v->type; f.msg = v->msg; f.seq = v->seq; f.ack = v->ack;
        f.len = (uint16_t)v->payload_len;
        memcpy(f.payload, v->payload, v->payload_len);

        size_t n = skw_frame_encode(&f, wire, sizeof wire);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->wire_len, n, v->name);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v->wire, wire, n, v->name);
    }
}

static void test_frame_decode_matches_vectors(void)
{
    for (size_t i = 0; i < FRAME_VECTOR_COUNT; i++) {
        const frame_vec_t *v = &FRAME_VECTORS[i];
        skw_frame_t f;
        /* skip both 0x00 delimiters: decode wants only the bytes between them */
        skw_status_t st = skw_frame_decode(v->wire + 1, v->wire_len - 2, &f);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SKW_OK, st, v->name);
        TEST_ASSERT_EQUAL_UINT8(v->type, f.type);
        TEST_ASSERT_EQUAL_UINT8(v->msg, f.msg);
        TEST_ASSERT_EQUAL_UINT16(v->seq, f.seq);
        TEST_ASSERT_EQUAL_UINT16(v->ack, f.ack);
        TEST_ASSERT_EQUAL_UINT16(v->payload_len, f.len);
        if (v->payload_len > 0) {   /* Unity refuses to compare zero elements (the ACK vector has none) */
            TEST_ASSERT_EQUAL_HEX8_ARRAY(v->payload, f.payload, v->payload_len);
        }
    }
}

static void test_frame_decode_rejects_damage(void)
{
    /* take the PING frame (vector 0), flip one bit in the middle, expect a CRC error */
    const frame_vec_t *v = &FRAME_VECTORS[0];
    uint8_t seg[SKW_MAX_WIRE];
    size_t len = v->wire_len - 2;
    memcpy(seg, v->wire + 1, len);
    seg[len / 2] ^= 0x04;
    skw_frame_t f;
    skw_status_t st = skw_frame_decode(seg, len, &f);
    TEST_ASSERT_TRUE(st == SKW_ERR_CRC || st == SKW_ERR_COBS);

    const uint8_t tiny[] = { 0x03, 0x11, 0x22 };   /* valid COBS, but only 2 bytes: too short for a frame */
    TEST_ASSERT_EQUAL_INT(SKW_ERR_SHORT, skw_frame_decode(tiny, sizeof tiny, &f));
}

static void test_frame_encode_rejects_long_payload(void)
{
    skw_frame_t f;
    uint8_t wire[SKW_MAX_WIRE];
    memset(&f, 0, sizeof f);
    f.len = SKW_MAX_PAYLOAD + 1;
    TEST_ASSERT_EQUAL_UINT32(0, skw_frame_encode(&f, wire, sizeof wire));
}

/* ---- parser ------------------------------------------------------------- */
/* Feed a byte array and count events; remember the last frame. */
typedef struct { int frames, bad, text; skw_frame_t last; uint8_t text_bytes[64]; } tally_t;

static void feed_all(skw_parser_t *p, const uint8_t *bytes, size_t n, tally_t *t)
{
    for (size_t i = 0; i < n; i++) {
        skw_event_t ev = skw_parser_feed(p, bytes[i], &t->last);
        if (ev == SKW_EV_FRAME) { t->frames++; }
        else if (ev == SKW_EV_BAD) { t->bad++; }
        else if (ev == SKW_EV_TEXT) { if (t->text < 64) { t->text_bytes[t->text] = p->text_byte; } t->text++; }
    }
}

static void test_parser_back_to_back_frames(void)
{
    skw_parser_t p; tally_t t; memset(&t, 0, sizeof t);
    skw_parser_init(&p);
    for (size_t i = 0; i < FRAME_VECTOR_COUNT; i++) {       /* all vectors in one stream */
        feed_all(&p, FRAME_VECTORS[i].wire, FRAME_VECTORS[i].wire_len, &t);
    }
    TEST_ASSERT_EQUAL_INT((int)FRAME_VECTOR_COUNT, t.frames);
    TEST_ASSERT_EQUAL_INT(0, t.bad);
    TEST_ASSERT_EQUAL_INT(0, t.text);
}

static void test_parser_text_outside_frames(void)
{
    skw_parser_t p; tally_t t; memset(&t, 0, sizeof t);
    skw_parser_init(&p);
    const uint8_t text[] = "help\r";                           /* typed console line */
    feed_all(&p, text, 5, &t);
    feed_all(&p, FRAME_VECTORS[0].wire, FRAME_VECTORS[0].wire_len, &t);
    feed_all(&p, text, 5, &t);
    TEST_ASSERT_EQUAL_INT(1, t.frames);
    TEST_ASSERT_EQUAL_INT(10, t.text);
    TEST_ASSERT_EQUAL_HEX8('h', t.text_bytes[0]);
}

static void test_parser_recovers_after_junk_and_overflow(void)
{
    skw_parser_t p; tally_t t; memset(&t, 0, sizeof t);
    skw_parser_init(&p);
    uint8_t junk[400];
    junk[0] = 0x00;                                            /* opening delimiter */
    memset(&junk[1], 0x55, sizeof junk - 2);                   /* far too long */
    junk[sizeof junk - 1] = 0x00;                              /* closing delimiter */
    feed_all(&p, junk, sizeof junk, &t);
    TEST_ASSERT_EQUAL_INT(1, t.bad);                           /* reported as one bad frame */
    feed_all(&p, FRAME_VECTORS[2].wire, FRAME_VECTORS[2].wire_len, &t);
    TEST_ASSERT_EQUAL_INT(1, t.frames);                        /* and a good frame still works */
    TEST_ASSERT_EQUAL_UINT8(0x10, t.last.msg);
}

static void test_parser_counts_bad_frames(void)
{
    skw_parser_t p; tally_t t; memset(&t, 0, sizeof t);
    skw_parser_init(&p);
    uint8_t wire[SKW_MAX_WIRE];
    size_t n = FRAME_VECTORS[1].wire_len;
    memcpy(wire, FRAME_VECTORS[1].wire, n);
    wire[n / 2] ^= 0x10;                                        /* damage one byte */
    feed_all(&p, wire, n, &t);
    TEST_ASSERT_EQUAL_INT(0, t.frames);
    TEST_ASSERT_EQUAL_INT(1, t.bad);
    TEST_ASSERT_EQUAL_UINT32(1, p.frames_bad);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_check_value);
    RUN_TEST(test_cobs_vectors_encode_and_decode);
    RUN_TEST(test_cobs_decode_rejects_zero_inside);
    RUN_TEST(test_frame_encode_matches_vectors);
    RUN_TEST(test_frame_decode_matches_vectors);
    RUN_TEST(test_frame_decode_rejects_damage);
    RUN_TEST(test_frame_encode_rejects_long_payload);
    RUN_TEST(test_parser_back_to_back_frames);
    RUN_TEST(test_parser_text_outside_frames);
    RUN_TEST(test_parser_recovers_after_junk_and_overflow);
    RUN_TEST(test_parser_counts_bad_frames);
    return UNITY_END();
}
