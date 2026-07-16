#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ngx_http_ws_deflate_compress.h"


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
test_compress_roundtrip(void)
{
    ngx_ws_deflate_ctx_t  ctx;
    const char           *input = "Hello World! This is a test of WebSocket "
                                  "compression.";
    size_t                input_len = strlen(input);
    u_char                compressed[1024];
    u_char                decompressed[1024];
    size_t                compressed_len, decompressed_len;
    ngx_int_t             rc;

    TEST("compress roundtrip");

    rc = ngx_ws_deflate_ctx_init(&ctx, Z_DEFAULT_COMPRESSION, 1);
    ASSERT_EQ(rc, NGX_OK, "init failed");

    compressed_len = sizeof(compressed);
    rc = ngx_ws_deflate_compress(&ctx, (u_char *) input, input_len,
                                 compressed, &compressed_len);
    ASSERT_EQ(rc, NGX_OK, "compress failed");
    ASSERT_TRUE(compressed_len > 0, "compressed length should be > 0");

    decompressed_len = sizeof(decompressed);
    rc = ngx_ws_deflate_decompress(&ctx, compressed, compressed_len,
                                   decompressed, &decompressed_len);
    ASSERT_EQ(rc, NGX_OK, "decompress failed");
    ASSERT_EQ(decompressed_len, input_len, "decompressed length mismatch");
    ASSERT_TRUE(memcmp(decompressed, input, input_len) == 0,
                "decompressed data mismatch");

    ngx_ws_deflate_ctx_destroy(&ctx);
    PASS();
}


static void
test_multiple_messages_takeover(void)
{
    ngx_ws_deflate_ctx_t  ctx;
    const char           *msgs[] = {
        "First message payload for WebSocket compression testing.",
        "Second message payload, compressed with context takeover enabled.",
        "Third and final message payload in the takeover test sequence."
    };
    size_t  msg_lens[] = {
        strlen(msgs[0]),
        strlen(msgs[1]),
        strlen(msgs[2])
    };
    u_char    compressed[3][1024];
    u_char    decompressed[3][1024];
    size_t    compressed_lens[3], decompressed_lens[3];
    ngx_int_t rc;
    int       i;

    TEST("multiple messages with context takeover");

    rc = ngx_ws_deflate_ctx_init(&ctx, Z_DEFAULT_COMPRESSION, 1);
    ASSERT_EQ(rc, NGX_OK, "init failed");

    for (i = 0; i < 3; i++) {
        compressed_lens[i] = sizeof(compressed[i]);
        rc = ngx_ws_deflate_compress(&ctx, (u_char *) msgs[i], msg_lens[i],
                                     compressed[i], &compressed_lens[i]);
        ASSERT_EQ(rc, NGX_OK, "compress failed");
        ASSERT_TRUE(compressed_lens[i] > 0,
                    "compressed length should be > 0");

        decompressed_lens[i] = sizeof(decompressed[i]);
        rc = ngx_ws_deflate_decompress(&ctx, compressed[i],
                                       compressed_lens[i],
                                       decompressed[i],
                                       &decompressed_lens[i]);
        ASSERT_EQ(rc, NGX_OK, "decompress failed");
        ASSERT_EQ(decompressed_lens[i], msg_lens[i],
                  "decompressed length mismatch");
        ASSERT_TRUE(memcmp(decompressed[i], msgs[i], msg_lens[i]) == 0,
                    "decompressed data mismatch");
    }

    ngx_ws_deflate_ctx_destroy(&ctx);
    PASS();
}


static void
test_empty_payload(void)
{
    ngx_ws_deflate_ctx_t  ctx;
    u_char                compressed[1024];
    u_char                decompressed[1024];
    size_t                compressed_len, decompressed_len;
    ngx_int_t             rc;

    TEST("empty payload roundtrip");

    rc = ngx_ws_deflate_ctx_init(&ctx, Z_DEFAULT_COMPRESSION, 1);
    ASSERT_EQ(rc, NGX_OK, "init failed");

    compressed_len = sizeof(compressed);
    rc = ngx_ws_deflate_compress(&ctx, (u_char *) "", 0,
                                 compressed, &compressed_len);
    ASSERT_EQ(rc, NGX_OK, "compress failed");

    decompressed_len = sizeof(decompressed);
    rc = ngx_ws_deflate_decompress(&ctx, compressed, compressed_len,
                                   decompressed, &decompressed_len);
    ASSERT_EQ(rc, NGX_OK, "decompress failed");
    ASSERT_EQ(decompressed_len, (size_t) 0,
              "decompressed length should be 0");

    ngx_ws_deflate_ctx_destroy(&ctx);
    PASS();
}


static void
test_no_context_takeover(void)
{
    ngx_ws_deflate_ctx_t  ctx;
    const char           *msgs[] = {
        "First message without context takeover.",
        "Second message, stream is reset between each.",
        "Third and final message in no-takeover test."
    };
    size_t  msg_lens[] = {
        strlen(msgs[0]),
        strlen(msgs[1]),
        strlen(msgs[2])
    };
    u_char    compressed[3][1024];
    u_char    decompressed[3][1024];
    size_t    compressed_lens[3], decompressed_lens[3];
    ngx_int_t rc;
    int       i;

    TEST("multiple messages without context takeover");

    rc = ngx_ws_deflate_ctx_init(&ctx, Z_DEFAULT_COMPRESSION, 0);
    ASSERT_EQ(rc, NGX_OK, "init failed");

    for (i = 0; i < 3; i++) {
        compressed_lens[i] = sizeof(compressed[i]);
        rc = ngx_ws_deflate_compress(&ctx, (u_char *) msgs[i], msg_lens[i],
                                     compressed[i], &compressed_lens[i]);
        ASSERT_EQ(rc, NGX_OK, "compress failed");
        ASSERT_TRUE(compressed_lens[i] > 0,
                    "compressed length should be > 0");

        decompressed_lens[i] = sizeof(decompressed[i]);
        rc = ngx_ws_deflate_decompress(&ctx, compressed[i],
                                       compressed_lens[i],
                                       decompressed[i],
                                       &decompressed_lens[i]);
        ASSERT_EQ(rc, NGX_OK, "decompress failed");
        ASSERT_EQ(decompressed_lens[i], msg_lens[i],
                  "decompressed length mismatch");
        ASSERT_TRUE(memcmp(decompressed[i], msgs[i], msg_lens[i]) == 0,
                    "decompressed data mismatch");
    }

    ngx_ws_deflate_ctx_destroy(&ctx);
    PASS();
}


int
main(void)
{
    fprintf(stderr, "WebSocket Compression Tests\n");
    fprintf(stderr, "============================\n");

    test_compress_roundtrip();
    test_multiple_messages_takeover();
    test_empty_payload();
    test_no_context_takeover();

    fprintf(stderr, "\n%d tests run, %d failed\n", tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
