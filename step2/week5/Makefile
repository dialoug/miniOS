CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

PROGRAMS := 01_epoll_echo 02_slow_client

.PHONY: all clean

all: $(PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(PROGRAMS)
