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

test_runner: test_frame.c $(FRAME_SRC)
	$(CC) $(CFLAGS) $(NGX_INCS) $(MODULE_INC) -o $@ test_frame.c $(FRAME_SRC)

test: test_runner
	./test_runner

clean:
	rm -f test_runner
