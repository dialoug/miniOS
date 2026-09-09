CC ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -g

PROGRAMS := 01_argv_inspect 02_parse_command 03_mini_shell

.PHONY: all clean

all: $(PROGRAMS)

01_argv_inspect: 01_argv_inspect.c
	$(CC) $(CFLAGS) $< -o $@

02_parse_command: 02_parse_command.c command_parser.c command_parser.h
	$(CC) $(CFLAGS) 02_parse_command.c command_parser.c -o $@

03_mini_shell: 03_mini_shell.c command_parser.c command_parser.h
	$(CC) $(CFLAGS) 03_mini_shell.c command_parser.c -o $@

clean:
	rm -f $(PROGRAMS)
