CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

PROGRAMS := 01_arrays_and_pointers 02_structs_and_callbacks 03_manual_resources 04_macros_and_container_of 05_dynamic_array

.PHONY: all clean

all: $(PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(PROGRAMS)
