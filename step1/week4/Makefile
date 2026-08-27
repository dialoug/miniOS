CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g -pthread

ifeq ($(SANITIZE),thread)
CFLAGS += -fsanitize=thread -fno-omit-frame-pointer
LDFLAGS += -fsanitize=thread
endif

PROGRAMS := 01_thread_lifecycle 02_counter_race 03_bounded_queue

.PHONY: all clean

all: $(PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(PROGRAMS)
