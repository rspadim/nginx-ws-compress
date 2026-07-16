#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ngx_http_ws_deflate_frame.h"


static int  tests_run = 0;
static int  tests_failed = 0;

#define TEST(name)                                                    \
    do {                                                              \
        fprintf(stderr, "  %s ... ", name);                           \
        tests_run++;                                                  \
    } while (0)

#define PASS()                                                        \
    do {                                                              \
        fprintf(stderr, "ok\n");                                      \
    } while (0)

#define FAIL(msg)                                                     \
    do {                                                              \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        tests_failed++;                                               \
    } while (0)

#define ASSERT_EQ(a, b, msg)                                          \
    do {                                                              \
        if ((a) != (b)) {                                             \
            FAIL(msg);                                                \
            return;                                                   \
        }                                                             \
    } while (0)

#define ASSERT_TRUE(cond, msg)                                        \
    do {                                                              \
        if (!(cond)) {                                                \
            FAIL(msg);                                                \
            return;                                                   \
        }                                                             \
    } while (0)


static void
test_parse_basic_text_frame(void)
{
    ngx_ws_frame_t  frame;
    u_char          data[] = { 0x81, 0x05, 'H', 'e', 'l', 'l', 'o' };
    ngx_int_t       rc;

    TEST("parse basic text frame");

    rc = ngx_ws_frame_parse(data, sizeof(data), &frame);
    ASSERT_EQ(rc, NGX_OK, "expected NGX_OK");

    ASSERT_EQ(frame.fin, 1, "fin should be 1");
    ASSERT_EQ(frame.opcode, NGX_WS_OPCODE_TEXT, "opcode should be text");
    ASSERT_EQ(frame.masked, 0, "masked should be 0");
    ASSERT_EQ(frame.payload_len, 5, "payload_len should be 5");
    ASSERT_EQ(frame.header_len, 2, "header_len should be 2");
    ASSERT_TRUE(memcmp(frame.payload, "Hello", 5) == 0, "payload should be 'Hello'");

    PASS();
}


static void
test_parse_masked_frame(void)
{
    ngx_ws_frame_t  frame;
    u_char          data[] = {
        0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d,
        0x7f, 0x9f, 0x4d, 0x51, 0x58
    };
    ngx_int_t       rc;

    TEST("parse masked frame");

    rc = ngx_ws_frame_parse(data, sizeof(data), &frame);
    ASSERT_EQ(rc, NGX_OK, "expected NGX_OK");

    ASSERT_EQ(frame.fin, 1, "fin should be 1");
    ASSERT_EQ(frame.opcode, NGX_WS_OPCODE_TEXT, "opcode should be text");
    ASSERT_EQ(frame.masked, 1, "masked should be 1");
    ASSERT_EQ(frame.masking_key, 0x37fa213d, "masking_key mismatch");
    ASSERT_EQ(frame.payload_len, 5, "payload_len should be 5");
    ASSERT_EQ(frame.header_len, 6, "header_len should be 6");

    PASS();
}


static void
test_extended_16bit_length(void)
{
    ngx_ws_frame_t  frame;
    u_char         *data;
    size_t          payload_len = 256;
    size_t          total;
    ngx_int_t       rc;

    TEST("extended 16-bit length");

    total = 4 + payload_len;
    data = malloc(total);
    ASSERT_TRUE(data != NULL, "malloc failed");

    data[0] = 0x82;
    data[1] = 126;
    data[2] = (u_char) (payload_len >> 8);
    data[3] = (u_char)  payload_len;
    memset(data + 4, 0x42, payload_len);

    rc = ngx_ws_frame_parse(data, total, &frame);
    ASSERT_EQ(rc, NGX_OK, "expected NGX_OK");

    ASSERT_EQ(frame.fin, 1, "fin should be 1");
    ASSERT_EQ(frame.opcode, NGX_WS_OPCODE_BINARY, "opcode should be binary");
    ASSERT_EQ(frame.masked, 0, "masked should be 0");
    ASSERT_EQ(frame.payload_len, payload_len, "payload_len should be 256");
    ASSERT_EQ(frame.header_len, 4, "header_len should be 4");

    free(data);
    PASS();
}


static void
test_partial_frame(void)
{
    ngx_ws_frame_t  frame;
    u_char          data[] = { 0x81, 0x05, 'H', 'e' };
    ngx_int_t       rc;

    TEST("partial frame returns NGX_AGAIN");

    rc = ngx_ws_frame_parse(data, sizeof(data), &frame);
    ASSERT_EQ(rc, NGX_AGAIN, "expected NGX_AGAIN for incomplete payload");

    /* Even smaller: only 1 byte */
    rc = ngx_ws_frame_parse(data, 1, &frame);
    ASSERT_EQ(rc, NGX_AGAIN, "expected NGX_AGAIN for 1 byte");

    /* Missing extended length bytes */
    data[1] = 126;
    rc = ngx_ws_frame_parse(data, 2, &frame);
    ASSERT_EQ(rc, NGX_AGAIN, "expected NGX_AGAIN for missing extended length");

    data[1] = 5;
    PASS();
}


static void
test_serialize_parse_roundtrip(void)
{
    ngx_ws_frame_t  frame, parsed;
    u_char          wire[32];
    u_char          payload[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    size_t          wire_len;
    ngx_int_t       rc;

    TEST("serialize/parse roundtrip");

    frame.fin    = 1;
    frame.rsv1   = 0;
    frame.rsv2   = 0;
    frame.rsv3   = 0;
    frame.opcode = NGX_WS_OPCODE_BINARY;
    frame.masked = 1;
    frame.masking_key = 0xdeadbeef;
    frame.payload = payload;
    frame.payload_len = 5;

    rc = ngx_ws_frame_serialize(&frame, wire, &wire_len);
    ASSERT_EQ(rc, NGX_OK, "serialize should return NGX_OK");
    ASSERT_TRUE(wire_len > 0, "wire_len should be > 0");

    rc = ngx_ws_frame_parse(wire, wire_len, &parsed);
    ASSERT_EQ(rc, NGX_OK, "parse after serialize should return NGX_OK");

    ASSERT_EQ(parsed.fin, 1, "fin should match");
    ASSERT_EQ(parsed.opcode, NGX_WS_OPCODE_BINARY, "opcode should match");
    ASSERT_EQ(parsed.masked, 1, "masked should match");
    ASSERT_EQ(parsed.masking_key, 0xdeadbeef, "masking_key should match");
    ASSERT_EQ(parsed.payload_len, 5, "payload_len should match");

    PASS();
}


static void
test_max_payload_exceeded(void)
{
    ngx_ws_frame_t  frame;
    u_char          data[10];
    ngx_int_t       rc;

    TEST("max payload exceeded");

    /* fin + opcode binary, 127 = 64-bit extended length */
    data[0] = 0x82;
    data[1] = 127;
    memset(data + 2, 0, 8);
    /* Set payload to NGX_WS_MAX_PAYLOAD + 1 */
    data[2] = 0x01;
    data[3] = 0x00;
    data[4] = 0x00;
    data[5] = 0x00;

    rc = ngx_ws_frame_parse(data, sizeof(data), &frame);
    ASSERT_EQ(rc, NGX_ERROR, "expected NGX_ERROR for oversized payload");

    PASS();
}


int
main(void)
{
    fprintf(stderr, "WebSocket Frame Parser Tests\n");
    fprintf(stderr, "============================\n");

    test_parse_basic_text_frame();
    test_parse_masked_frame();
    test_extended_16bit_length();
    test_partial_frame();
    test_serialize_parse_roundtrip();
    test_max_payload_exceeded();

    fprintf(stderr, "\n%d tests run, %d failed\n", tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
