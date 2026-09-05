CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g

PROGRAMS := 01_fork_identity 02_exec_wait 03_pipe_parent_child 04_exec_pipeline

.PHONY: all clean

all: $(PROGRAMS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(PROGRAMS)
