CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g

PROGRAMS := 01_mini_find 02_mmap_reader 03_locked_counter \
	04_fifo_server 04_fifo_client 05_unix_server 05_unix_client \
	06_epoll_unix_server

.PHONY: all clean

all: $(PROGRAMS)

01_mini_find: 01_mini_find.c
	$(CC) $(CFLAGS) $< -o $@

02_mmap_reader: 02_mmap_reader.c
	$(CC) $(CFLAGS) $< -o $@

03_locked_counter: 03_locked_counter.c
	$(CC) $(CFLAGS) $< -o $@

04_fifo_server: 04_fifo_server.c
	$(CC) $(CFLAGS) $< -o $@

04_fifo_client: 04_fifo_client.c
	$(CC) $(CFLAGS) $< -o $@

05_unix_server: 05_unix_server.c
	$(CC) $(CFLAGS) $< -o $@

05_unix_client: 05_unix_client.c
	$(CC) $(CFLAGS) $< -o $@

06_epoll_unix_server: 06_epoll_unix_server.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(PROGRAMS)
