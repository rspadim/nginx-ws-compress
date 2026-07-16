CC = gcc
CFLAGS = -Wall -Wextra -g
NGX_INC = -I /usr/local/include
CFLAGS += -DNGX_OK=0 -DNGX_AGAIN=-2 -DNGX_ERROR=-1

# Point NGX_SRC to the nginx source tree. Default assumes WSL path.
NGX_SRC ?= /root/nginx-src

NGX_INCS = -I $(NGX_SRC)/src/core -I $(NGX_SRC)/src/event
NGX_INCS += -I $(NGX_SRC)/src/http -I $(NGX_SRC)/src/os/unix
NGX_INCS += -I $(NGX_SRC)/objs $(NGX_INC)

MODULE_INC = -I ../../ngx_http_ws_deflate_module

FRAME_SRC = ../../ngx_http_ws_deflate_module/ngx_http_ws_deflate_frame.c
COMPRESS_SRC = ../../ngx_http_ws_deflate_module/ngx_http_ws_deflate_compress.c

test_frame: test_frame.c $(FRAME_SRC)
	$(CC) $(CFLAGS) $(NGX_INCS) $(MODULE_INC) -o $@ test_frame.c $(FRAME_SRC)

test_compress: test_compress.c $(COMPRESS_SRC)
	$(CC) $(CFLAGS) $(NGX_INCS) $(MODULE_INC) -o $@ test_compress.c $(COMPRESS_SRC) -lz

test: test_frame test_compress
	./test_frame && ./test_compress

clean:
	rm -f test_frame test_compress
