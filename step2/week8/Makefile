CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g

PROGRAMS := 01_http_server

.PHONY: all clean

all: $(PROGRAMS)

01_http_server: 01_http_server.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(PROGRAMS)
