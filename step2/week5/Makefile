CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

.PHONY: all clean

all: 01_epoll_echo

01_epoll_echo: 01_epoll_echo.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f 01_epoll_echo
