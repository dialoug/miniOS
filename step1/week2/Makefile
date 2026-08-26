CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

PROGRAMS := 01_memory_layout 02_undefined_behavior

.PHONY: all clean

all: $(PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(PROGRAMS)
