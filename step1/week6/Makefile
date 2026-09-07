CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g

PROGRAMS := 01_mini_cp

.PHONY: all clean

all: $(PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(PROGRAMS)
